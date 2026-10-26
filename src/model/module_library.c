/** @file module_library.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Check actual loaded image ranges before reading module-v1 exports.
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#    define _GNU_SOURCE
#endif
#include "module_library.h"

#include "lib/allocate.h"
#include "lib/file_system.h"
#include "lib/windows_target.h"
#include "lib/wire.h"
#include "native_library_platform.h"
#include "vm/module_consistency.h"

#include <inttypes.h>
#include <string.h>
#ifdef __linux__
#    include <dlfcn.h>
#    include <link.h>
#elif defined(_WIN32)
#    include <windows.h>
#endif

typedef struct {
    char *symbol;
    goat_module_adapter_v1 adapter;
} module_symbol_t;

#ifdef __linux__
typedef struct {
    uintptr_t first, last;
    bool executable;
} image_range_t;
#endif

struct module_library_t {
    void *handle;
    module_symbol_t *symbols;
    size_t count;
#ifdef __linux__
    uintptr_t base;
    image_range_t *ranges;
    size_t range_count;
#endif
};

#ifdef __linux__
static int collect_ranges(struct dl_phdr_info *info, size_t size, void *state) {
    (void)size;
    module_library_t *library = state;
    if ((uintptr_t)info->dlpi_addr != library->base)
        return 0;
    library->ranges = CALLOC(allocation_size_multiply(info->dlpi_phnum, sizeof(image_range_t)));
    for (size_t i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *header = info->dlpi_phdr + i;
        if (header->p_type != PT_LOAD || !(header->p_flags & PF_R)
            || header->p_vaddr > UINTPTR_MAX - library->base)
            continue;
        uintptr_t begin = library->base + header->p_vaddr;
        if (header->p_memsz > UINTPTR_MAX - begin)
            continue;
        library->ranges[library->range_count++] =
            (image_range_t){begin, begin + header->p_memsz, (header->p_flags & PF_X) != 0};
    }
    return 1;
}

static bool owned_range(module_library_t *library, uintptr_t first, uintptr_t last, bool code) {
    while (first < last) {
        uintptr_t next = first;
        for (size_t i = 0; i < library->range_count; i++) {
            image_range_t *range = library->ranges + i;
            if (range->first <= first && first < range->last && (!code || range->executable)
                && range->last > next)
                next = range->last;
        }
        if (next == first)
            return false;
        first = next;
    }
    return true;
}

/* PT_LOAD establishes ownership, not current access: RELRO and constructors can
 * change protections. Check the actual mappings, failing closed without procfs.
 * Trusted native code must keep returned immutable memory stable during a call;
 * this validation does not pretend to sandbox code that races mprotect/unmap.
 */
static bool image_range(module_library_t *library, const void *pointer, size_t length, bool code) {
    uintptr_t first = (uintptr_t)pointer;
    if (!pointer || !length || length > UINTPTR_MAX - first
        || !owned_range(library, first, first + length, code))
        return false;
    uintptr_t last = first + length;
    FILE *maps = FOPEN("/proc/self/maps", "rb");
    if (!maps)
        return false;
    char line[4096];
    while (first < last && FGETS(line, sizeof(line), maps)) {
        /* A pathname may exceed this buffer. Only the first fragment contains
           a mapping header; never interpret its continuation as another map. */
        if (!strchr(line, '\n')) {
            int character;
            do {
                character = FGETC(maps);
            } while (character != '\n' && character != EOF);
        }
        uintptr_t begin, end;
        char permissions[5];
        if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %4s", &begin, &end, permissions) != 3
            || begin > first || end <= first)
            continue;
        if (permissions[0] != 'r' || permissions[1] == 'w' || (code && permissions[2] != 'x'))
            break;
        first = end;
    }
    bool ok = first >= last && !FERROR(maps);
    FCLOSE(maps);
    return ok;
}

static void *symbol_address(module_library_t *library, const char *symbol) {
    dlerror();
    void *address = dlsym(library->handle, symbol);
    return dlerror() ? NULL : address;
}
#elif defined(_WIN32)
static bool image_range(module_library_t *library, const void *pointer, size_t length, bool code) {
    uintptr_t first = (uintptr_t)pointer;
    if (!pointer || !length || length > UINTPTR_MAX - first)
        return false;
    uintptr_t last = first + length;
    while (first < last) {
        MEMORY_BASIC_INFORMATION information;
        if (!VirtualQuery((void *)first, &information, sizeof(information))
            || information.AllocationBase != library->handle || information.State != MEM_COMMIT
            || information.Type != MEM_IMAGE || (information.Protect & PAGE_GUARD))
            return false;
        DWORD protection = information.Protect & 0xff;
        if (code ? protection != PAGE_EXECUTE_READ
                 : protection != PAGE_READONLY && protection != PAGE_EXECUTE_READ)
            return false;
        uintptr_t begin = (uintptr_t)information.BaseAddress;
        if (information.RegionSize > UINTPTR_MAX - begin || begin + information.RegionSize <= first)
            return false;
        first = begin + information.RegionSize;
    }
    return true;
}

static void *symbol_address(module_library_t *library, const char *symbol) {
    FARPROC function = GetProcAddress((HMODULE)library->handle, symbol);
    void *address = NULL;
    _Static_assert(sizeof(function) == sizeof(address), "Windows symbol pointer representation");
    memcpy(&address, &function, sizeof(address));
    return address;
}
#else
static bool image_range(module_library_t *library, const void *pointer, size_t length, bool code) {
    (void)library;
    (void)pointer;
    (void)length;
    (void)code;
    return false;
}

static void *symbol_address(module_library_t *library, const char *symbol) {
    (void)library;
    (void)symbol;
    return NULL;
}
#endif

static bool readable(void *state, const void *pointer, size_t length) {
    return image_range(state, pointer, length, false);
}

void release_module_library(module_library_t *library) {
    if (!library)
        return;
    for (size_t i = 0; i < library->count; i++)
        FREE(library->symbols[i].symbol);
    FREE(library->symbols);
#ifdef __linux__
    FREE(library->ranges);
#endif
    if (library->handle)
        close_native_library_handle(library->handle);
    FREE(library);
}

static bool validate_descriptor(module_library_t *library,
                                const yyjson_val *metadata,
                                const yyjson_val *variant) {
    const unsigned char *address = symbol_address(library, "goat_module_descriptor_v1");
    if (!readable(library, address, 16))
        return false;
    unsigned char header[16];
    memcpy(header, address, sizeof(header));
    if (memcmp(header, "GOATND01", 8))
        return false;
    uint64_t length = (uint64_t)read_le32(header + 8) | (uint64_t)read_le32(header + 12) << 32;
    if (length > SIZE_MAX - 17 || !readable(library, address, (size_t)length + 17))
        return false;
    size_t size = (size_t)length + 17;
    void *copy = ALLOC(size);
    memcpy(copy, address, size);
    bool ok =
        match_module_descriptor_metadata(metadata, variant, copy, size) == MODULE_CONSISTENCY_OK;
    FREE(copy);
    return ok;
}

module_library_result_t
load_module_library(const char *path, const yyjson_doc *metadata, size_t variant_index) {
    module_library_result_t result = {0};
    const yyjson_val *root = metadata ? yyjson_doc_get_root((yyjson_doc *)metadata) : NULL;
    const yyjson_val *variant = yyjson_arr_get(yyjson_obj_get(root, "native"), variant_index);
    if (!path || !variant || goat_module_abi_validate(variant) != GOAT_ABI_OK) {
        result.diagnostic = copy_native_library_diagnostic("Invalid module-v1 loader arguments");
        return result;
    }
    module_library_t *library = CALLOC(sizeof(*library));
    library->handle = open_native_library_handle(path, &result.diagnostic);
    const char *failure = NULL;
    if (!library->handle)
        goto failed;
#ifdef __linux__
    struct link_map *map = NULL;
    if (dlinfo(library->handle, RTLD_DI_LINKMAP, &map) || !map) {
        failure = "Cannot establish loaded module image ownership";
        goto failed;
    }
    library->base = (uintptr_t)map->l_addr;
    dl_iterate_phdr(collect_ranges, library);
    if (!library->range_count) {
        failure = "Cannot establish loaded module image ranges";
        goto failed;
    }
#endif
    if (!validate_descriptor(library, root, variant)) {
        failure = "Native module descriptor is unreadable, mutable, foreign or mismatched";
        goto failed;
    }
    const yyjson_val *specs = yyjson_obj_get(variant, "specializations");
    size_t count = yyjson_arr_size(specs);
    library->symbols = CALLOC(allocation_size_multiply(count, sizeof(module_symbol_t)));
    for (size_t i = 0; i < count; i++) {
        const char *name = yyjson_get_str(yyjson_obj_get(yyjson_arr_get(specs, i), "symbol"));
        void *address = name ? symbol_address(library, name) : NULL;
        if (!image_range(library, address, 1, true)) {
            failure = "Native module adapter symbol is absent, foreign or not executable";
            goto failed;
        }
        module_symbol_t *symbol = library->symbols + library->count++;
        symbol->symbol = copy_native_library_diagnostic(name);
        _Static_assert(sizeof(symbol->adapter) == sizeof(address),
                       "Adapter pointer representation");
        memcpy(&symbol->adapter, &address, sizeof(address));
    }
    result.library = library;
    return result;
failed:
    if (failure) {
        FREE(result.diagnostic);
        result.diagnostic = copy_native_library_diagnostic(failure);
    }
    release_module_library(library);
    return result;
}

bool resolve_module_library_entry(module_library_t *library,
                                  const char *symbol,
                                  module_native_entry_t *out) {
    if (out)
        memset(out, 0, sizeof(*out));
    if (!library || !symbol || !out)
        return false;
    for (size_t i = 0; i < library->count; i++) {
        if (strcmp(library->symbols[i].symbol, symbol))
            continue;
        out->adapter = library->symbols[i].adapter;
        out->readable = readable;
        out->readable_state = library;
        return true;
    }
    return false;
}
