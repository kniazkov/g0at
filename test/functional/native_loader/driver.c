/** @file driver.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Loader validation and real unload checks, independent of the VM.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/allocate.h"
#include "model/native_library.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#endif

static void environment(const char *name, const char *value) {
#ifdef _WIN32
    assert(SetEnvironmentVariableA(name, value));
#else
    assert(!setenv(name, value, 1));
#endif
}

static long unloads(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file)
        return 0;
    assert(!fseek(file, 0, SEEK_END));
    long size = ftell(file);
    fclose(file);
    return size;
}

static void rejected(const char *path, native_library_status_t status) {
    size_t before = get_allocated_memory_size();
    native_library_result_t result = load_native_library(path);
    assert(result.status == status && !result.library && result.diagnostic && result.diagnostic[0]);
    destroy_native_library_result(&result);
    destroy_native_library_result(&result);
    assert(get_allocated_memory_size() == before);
}

int main(int argc, char **argv) {
    assert(argc == 6);
    const char *generated = argv[1], *provider = argv[2], *missing_query = argv[3];
    const char *missing_dependency = argv[4], *marker = argv[5];
    remove(marker);
    environment("GOAT_TEST_UNLOAD", marker);
    rejected(NULL, NATIVE_LIBRARY_OPEN_FAILED);
    rejected("", NATIVE_LIBRARY_OPEN_FAILED);
    rejected("does-not-exist-native-library", NATIVE_LIBRARY_OPEN_FAILED);
    rejected(marker, NATIVE_LIBRARY_OPEN_FAILED);
    rejected(missing_query, NATIVE_LIBRARY_QUERY_MISSING);
    assert(unloads(marker) == 1);
    rejected(missing_dependency, NATIVE_LIBRARY_OPEN_FAILED);
    assert(unloads(marker) == 1);
    for (int which = 1; which <= 33; which++) {
        char text[20];
        snprintf(text, sizeof(text), "%d", which);
        environment("GOAT_TEST_METADATA", text);
        long before = unloads(marker);
        rejected(provider,
                 which <= 7 || which == 26 ? NATIVE_LIBRARY_ABI_MISMATCH
                                           : NATIVE_LIBRARY_INVALID_METADATA);
        assert(unloads(marker) == before + 1);
    }
    /* The marker is now a real file, but it is not a native library. */
    rejected(marker, NATIVE_LIBRARY_OPEN_FAILED);
    environment("GOAT_TEST_METADATA", "0");
    size_t baseline = get_allocated_memory_size();
    long before = unloads(marker);
    native_library_result_t result = load_native_library(provider);
    assert(result.status == NATIVE_LIBRARY_OK && result.library && !result.diagnostic);
    assert(get_native_library_entry_count(result.library) == 2);
    assert(!get_native_library_entry(result.library, 2));
    assert(!create_native_function_descriptor(result.library, 123));
    native_library_t *library = retain_native_library(result.library);
    native_function_descriptor_t *function = create_native_function_descriptor(library, 7);
    assert(function && get_native_function_entry_count(function) == 2);
    assert(!get_native_function_entry(function, 2));
    native_function_descriptor_t *alias = retain_native_function_descriptor(function);
    /* A second query deliberately mutates the fixture's source metadata. */
    environment("GOAT_TEST_METADATA", "41");
    native_library_result_t second = load_native_library(provider);
    assert(second.status == NATIVE_LIBRARY_OK);
    assert(!strcmp(get_native_library_entry(second.library, 0)->binding_name, "fixture.\xc3\xa9"));
    destroy_native_library_result(&second);
    destroy_native_library_result(&result);
    release_native_library(library);
    assert(unloads(marker) == before);
    const goat_native_entry_v1_t *entry = get_native_function_entry(function, 0);
    assert(entry->function_id == 7 && entry->specialization_id == 3);
    assert(entry->parameter_types[0] == GOAT_NATIVE_I64 && entry->flags == GOAT_NATIVE_PURE);
    assert(!strcmp(entry->binding_name, "fixture.identity"));
    goat_native_value_v1_t arg = {.type = GOAT_NATIVE_I64, .value.integer = INT64_MAX}, value = {0};
    assert(entry->invoke(1, 1, &arg, &value) == GOAT_NATIVE_OK && value.value.integer == INT64_MAX);
    release_native_function_descriptor(function);
    assert(unloads(marker) == before);
    entry = get_native_function_entry(alias, 1);
    arg = (goat_native_value_v1_t){.type = GOAT_NATIVE_F64, .value.real = 0.25};
    assert(entry->invoke(1, 1, &arg, &value) == GOAT_NATIVE_OK && value.value.real == 0.25);
    release_native_function_descriptor(alias);
    assert(unloads(marker) == before + 1);
    assert(get_allocated_memory_size() == baseline);
    environment("GOAT_TEST_METADATA", "40");
    result = load_native_library(provider);
    assert(result.status == NATIVE_LIBRARY_OK && !get_native_library_entry_count(result.library));
    assert(!create_native_function_descriptor(result.library, 7));
    destroy_native_library_result(&result);
    assert(unloads(marker) == before + 2);
    result = load_native_library(generated);
    assert(result.status == NATIVE_LIBRARY_OK
           && get_native_library_entry_count(result.library) == 6);
    function = create_native_function_descriptor(result.library, 4);
    assert(function && get_native_function_entry_count(function) == 1);
    destroy_native_library_result(&result);
    arg = (goat_native_value_v1_t){.type = GOAT_NATIVE_I64, .value.integer = 10};
    entry = get_native_function_entry(function, 0);
    assert(entry->invoke(1, 1, &arg, &value) == GOAT_NATIVE_OK && value.value.integer == 55);
    release_native_function_descriptor(function);
    release_native_library(NULL);
    release_native_function_descriptor(NULL);
    assert(!retain_native_library(NULL) && !retain_native_function_descriptor(NULL));
    assert(!get_native_library_entry(NULL, 0) && !get_native_function_entry(NULL, 0));
    assert(!get_native_library_entry_count(NULL) && !get_native_function_entry_count(NULL));
    assert(get_allocated_memory_size() == baseline);
    return 0;
}
