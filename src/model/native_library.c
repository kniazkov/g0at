/** @file native_library.c
 * @copyright 2026 Ivan Kniazkov
 * @brief ABI validation, metadata snapshots and reference-counted descriptors.
 */
#include "native_library.h"

#include "lib/allocate.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct native_library_t {
    atomic_size_t references;
    void *handle;
    uint32_t entry_count;
    goat_native_entry_v1_t *entries;
};

struct native_function_descriptor_t {
    atomic_size_t references;
    native_library_t *library;
    uint32_t entry_count;
    const goat_native_entry_v1_t **entries;
};

char *copy_native_library_diagnostic(const char *text) {
    size_t size = strlen(text) + 1;
    char *copy = ALLOC(size);
    memcpy(copy, text, size);
    return copy;
}

static bool numeric(uint32_t type) {
    return type == GOAT_NATIVE_I64 || type == GOAT_NATIVE_F64;
}

/** @brief Validates a bounded UTF-8 binding name without replacement decoding. */
static bool binding_length(const char *name, size_t *length) {
    size_t n = 0;
    while (n <= 4096 && name[n])
        n++;
    if (!n || n > 4096)
        return false;
    for (size_t i = 0; i < n;) {
        uint32_t value = (unsigned char)name[i++], minimum;
        unsigned extra;
        if (value < 0x80)
            continue;
        if (value >= 0xc2 && value <= 0xdf) {
            value &= 0x1f;
            extra = 1;
            minimum = 0x80;
        } else if (value >= 0xe0 && value <= 0xef) {
            value &= 0x0f;
            extra = 2;
            minimum = 0x800;
        } else if (value >= 0xf0 && value <= 0xf4) {
            value &= 7;
            extra = 3;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (extra > n - i)
            return false;
        while (extra--) {
            unsigned byte = (unsigned char)name[i++];
            if ((byte & 0xc0) != 0x80)
                return false;
            value = (value << 6) | (byte & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return false;
    }
    *length = n;
    return true;
}

static const char *
snapshot_entry(goat_native_entry_v1_t *copy, const goat_native_entry_v1_t *entry, size_t *budget) {
    if (!entry->invoke)
        return "missing adapter";
    if (!numeric(entry->return_type))
        return "invalid return type";
    if (entry->flags & ~(uint32_t)GOAT_NATIVE_PURE)
        return "unknown flags";
    if (entry->parameter_count > UINT16_MAX)
        return "too many formal parameters";
    if ((!entry->parameter_count) != (!entry->parameter_types))
        return "invalid parameter table";
    if (entry->parameter_types && (uintptr_t)entry->parameter_types % _Alignof(uint32_t))
        return "misaligned parameter table";
    size_t name_size = 0;
    if (entry->binding_name) {
        if (!binding_length(entry->binding_name, &name_size))
            return "invalid binding name";
        name_size++;
    }
    size_t bytes = entry->parameter_count * sizeof(uint32_t);
    if (bytes + name_size > *budget)
        return "metadata budget exceeded";
    *budget -= bytes + name_size;
    for (uint32_t i = 0; i < entry->parameter_count; i++)
        if (!numeric(entry->parameter_types[i]))
            return "invalid parameter type";
    copy->specialization_id = entry->specialization_id;
    copy->function_id = entry->function_id;
    copy->parameter_count = entry->parameter_count;
    copy->return_type = entry->return_type;
    copy->invoke = entry->invoke;
    copy->flags = entry->flags;
    if (bytes) {
        uint32_t *types = ALLOC(bytes);
        memcpy(types, entry->parameter_types, bytes);
        copy->parameter_types = types;
    }
    if (name_size)
        copy->binding_name = copy_native_library_diagnostic(entry->binding_name);
    return NULL;
}

static int compare_id(const void *a, const void *b) {
    uint64_t x = (*(const goat_native_entry_v1_t *const *)a)->specialization_id;
    uint64_t y = (*(const goat_native_entry_v1_t *const *)b)->specialization_id;
    return (x > y) - (x < y);
}

static int compare_signature(const void *a, const void *b) {
    const goat_native_entry_v1_t *x = *(const goat_native_entry_v1_t *const *)a;
    const goat_native_entry_v1_t *y = *(const goat_native_entry_v1_t *const *)b;
    if (x->function_id != y->function_id)
        return (x->function_id > y->function_id) ? 1 : -1;
    if (x->parameter_count != y->parameter_count)
        return (x->parameter_count > y->parameter_count) ? 1 : -1;
    for (uint32_t i = 0; i < x->parameter_count; i++)
        if (x->parameter_types[i] != y->parameter_types[i])
            return (x->parameter_types[i] > y->parameter_types[i]) ? 1 : -1;
    return 0;
}

static int compare_name(const void *a, const void *b) {
    const char *x = (*(const goat_native_entry_v1_t *const *)a)->binding_name;
    const char *y = (*(const goat_native_entry_v1_t *const *)b)->binding_name;
    if (!x || !y)
        return (x != NULL) - (y != NULL);
    return strcmp(x, y);
}

static const char *validate_duplicates(native_library_t *library) {
    uint32_t count = library->entry_count;
    if (!count)
        return NULL;
    const goat_native_entry_v1_t **sorted = ALLOC(count * sizeof(*sorted));
    for (uint32_t i = 0; i < count; i++)
        sorted[i] = &library->entries[i];
    const char *error = NULL;
    qsort(sorted, count, sizeof(*sorted), compare_id);
    for (uint32_t i = 1; i < count; i++)
        if (!compare_id(sorted + i - 1, sorted + i))
            error = "duplicate specialization ID";
    qsort(sorted, count, sizeof(*sorted), compare_signature);
    for (uint32_t i = 1; i < count; i++) {
        if (!compare_signature(sorted + i - 1, sorted + i))
            error = "duplicate function signature";
        if (sorted[i - 1]->function_id == sorted[i]->function_id
            && compare_name(sorted + i - 1, sorted + i))
            error = "inconsistent function binding name";
    }
    qsort(sorted, count, sizeof(*sorted), compare_name);
    for (uint32_t i = 1; i < count; i++)
        if (sorted[i]->binding_name && !compare_name(sorted + i - 1, sorted + i)
            && sorted[i - 1]->function_id != sorted[i]->function_id)
            error = "binding name shared by different functions";
    FREE(sorted);
    return error;
}

native_library_result_t load_native_library(const char *path) {
    native_library_result_t result = {.status = NATIVE_LIBRARY_OPEN_FAILED};
#if !defined(__linux__) && !defined(_WIN32)
    (void)path;
    result.status = NATIVE_LIBRARY_UNSUPPORTED;
    result.diagnostic = copy_native_library_diagnostic("Native libraries are unsupported");
    return result;
#else
    if (!path || !path[0]) {
        result.diagnostic = copy_native_library_diagnostic("Empty native library path");
        return result;
    }
    void *handle = open_native_library_handle(path, &result.diagnostic);
    if (!handle)
        return result;
    native_library_t *library = CALLOC(sizeof(*library));
    atomic_init(&library->references, 1);
    library->handle = handle;
    goat_native_query_v1_t query;
    result.status = NATIVE_LIBRARY_QUERY_MISSING;
    if (!get_native_library_query(handle, &query, &result.diagnostic))
        goto failed;
    result.status = NATIVE_LIBRARY_ABI_MISMATCH;
    const goat_native_module_v1_t *module = query(GOAT_NATIVE_ABI_VERSION);
    const char *error = "Incompatible native ABI";
    if (!module || (uintptr_t)module % _Alignof(goat_native_module_v1_t)
        || module->abi_version != GOAT_NATIVE_ABI_VERSION || module->struct_size != sizeof(*module))
        goto invalid;
    if (module->value_size != sizeof(goat_native_value_v1_t)
        || module->value_alignment != _Alignof(goat_native_value_v1_t)
        || module->entry_size != sizeof(goat_native_entry_v1_t)
        || module->pointer_size != sizeof(void *))
        goto invalid;
    result.status = NATIVE_LIBRARY_INVALID_METADATA;
    error = "Invalid native entry table";
    if (module->entry_count > 65536 || (!module->entry_count) != (!module->entries)
        || (module->entries && (uintptr_t)module->entries % _Alignof(goat_native_entry_v1_t)))
        goto invalid;
    library->entry_count = module->entry_count;
    if (library->entry_count)
        library->entries = CALLOC(library->entry_count * sizeof(*library->entries));
    size_t budget = 64 * 1024 * 1024;
    for (uint32_t i = 0; i < library->entry_count; i++) {
        error = snapshot_entry(&library->entries[i], &module->entries[i], &budget);
        if (error) {
            char message[160];
            snprintf(message, sizeof(message), "Invalid native entry %u: %s", i, error);
            result.diagnostic = copy_native_library_diagnostic(message);
            goto failed;
        }
    }
    error = validate_duplicates(library);
    if (error)
        goto invalid;
    result.status = NATIVE_LIBRARY_OK;
    result.library = library;
    return result;
invalid:
    result.diagnostic = copy_native_library_diagnostic(error);
failed:
    release_native_library(library);
    return result;
#endif
}

native_library_t *retain_native_library(native_library_t *library) {
    if (library && atomic_fetch_add(&library->references, 1) == SIZE_MAX)
        abort();
    return library;
}

void release_native_library(native_library_t *library) {
    if (!library || atomic_fetch_sub(&library->references, 1) != 1)
        return;
    for (uint32_t i = 0; i < library->entry_count; i++) {
        FREE((void *)library->entries[i].parameter_types);
        FREE((void *)library->entries[i].binding_name);
    }
    FREE(library->entries);
    close_native_library_handle(library->handle);
    FREE(library);
}

void destroy_native_library_result(native_library_result_t *result) {
    release_native_library(result->library);
    FREE(result->diagnostic);
    result->library = NULL;
    result->diagnostic = NULL;
}

uint32_t get_native_library_entry_count(const native_library_t *library) {
    return library ? library->entry_count : 0;
}

const goat_native_entry_v1_t *get_native_library_entry(const native_library_t *library,
                                                       uint32_t index) {
    return library && index < library->entry_count ? &library->entries[index] : NULL;
}

native_function_descriptor_t *create_native_function_descriptor(native_library_t *library,
                                                                uint64_t function_id) {
    uint32_t count = 0;
    if (!library)
        return NULL;
    for (uint32_t i = 0; i < library->entry_count; i++)
        count += library->entries[i].function_id == function_id;
    if (!count)
        return NULL;
    native_function_descriptor_t *function = CALLOC(sizeof(*function));
    atomic_init(&function->references, 1);
    function->library = retain_native_library(library);
    function->entry_count = count;
    function->entries = ALLOC(count * sizeof(*function->entries));
    uint32_t next = 0;
    for (uint32_t i = 0; i < library->entry_count; i++)
        if (library->entries[i].function_id == function_id)
            function->entries[next++] = &library->entries[i];
    return function;
}

native_function_descriptor_t *
retain_native_function_descriptor(native_function_descriptor_t *function) {
    if (function && atomic_fetch_add(&function->references, 1) == SIZE_MAX)
        abort();
    return function;
}

void release_native_function_descriptor(native_function_descriptor_t *function) {
    if (!function || atomic_fetch_sub(&function->references, 1) != 1)
        return;
    FREE(function->entries);
    release_native_library(function->library);
    FREE(function);
}

uint32_t get_native_function_entry_count(const native_function_descriptor_t *function) {
    return function ? function->entry_count : 0;
}

const goat_native_entry_v1_t *
get_native_function_entry(const native_function_descriptor_t *function, uint32_t index) {
    return function && index < function->entry_count ? function->entries[index] : NULL;
}

#if !defined(__linux__) && !defined(_WIN32)
void close_native_library_handle(void *handle) {
    (void)handle;
}
#endif
