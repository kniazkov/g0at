/** @file module_library.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Check actual loaded image ranges before reading module-v1 exports.
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#    define _GNU_SOURCE
#endif
#include "module_library.h"

#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/wire/wire.h"
#include "module_library_platform.h"
#include "native_library_platform.h"
#include "vm/module_consistency.h"

#include <inttypes.h>
#include <string.h>
#ifdef __linux__
#    include <dlfcn.h>
#    include <link.h>
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
    const ElfW(Dyn) * dynamic;
    size_t dynamic_count;
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
        if (header->p_type == PT_DYNAMIC && header->p_vaddr <= UINTPTR_MAX - library->base) {
            library->dynamic = (const ElfW(Dyn) *)(library->base + header->p_vaddr);
            library->dynamic_count = header->p_memsz / sizeof(ElfW(Dyn));
        }
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

/* Executable ELF segments may also contain read-only objects (notably on ARM).
 * Check the dynamic symbol type as well as page access before treating an
 * address as a callable adapter. A nearest containing symbol is insufficient.
 */
/* musl has no dladdr1/RTLD_DL_SYMENT. More importantly, nearest-symbol
 * queries cannot distinguish differently typed aliases at one address. Read the loaded
 * dynamic symbol table instead. Every span must belong to immutable readable
 * mappings of this image; hash tables bound the symbol count, not heuristics
 * based on adjacent sections. Match the exact requested export name and type.
 * Dynamic pointers remain image-relative on musl; glibc usually relocates them.
 */
static const void *dynamic_pointer(module_library_t *library, uintptr_t offset, size_t size) {
#    ifdef __GLIBC__
    const void *absolute = (const void *)offset;
    if (image_range(library, absolute, size, false))
        return absolute;
#    endif
    if (offset > UINTPTR_MAX - library->base)
        return NULL;
    const void *pointer = (const void *)(library->base + offset);
    return image_range(library, pointer, size, false) ? pointer : NULL;
}

static size_t dynamic_symbol_count(module_library_t *library, uintptr_t hash, uintptr_t gnu_hash) {
    const size_t limit = 1048576;
    if (hash) {
        const uint32_t *table = dynamic_pointer(library, hash, 2 * sizeof(uint32_t));
        if (!table || (uintptr_t)table % _Alignof(uint32_t) || !table[0] || table[0] > limit
            || !table[1] || table[1] > limit
            || !dynamic_pointer(library, hash, (2 + (size_t)table[0] + table[1]) * 4))
            return 0;
        return table[1];
    }
    const uint32_t *table = dynamic_pointer(library, gnu_hash, 4 * sizeof(uint32_t));
    if (!gnu_hash || !table || (uintptr_t)table % _Alignof(uint32_t) || !table[0]
        || table[0] > limit || table[1] > limit || !table[2] || table[2] > limit)
        return 0;
    size_t prefix = 16 + (size_t)table[2] * sizeof(ElfW(Addr));
    size_t bytes = prefix + (size_t)table[0] * 4;
    if (!dynamic_pointer(library, gnu_hash, bytes))
        return 0;
    const uint32_t *buckets = (const uint32_t *)((const unsigned char *)table + prefix);
    uint32_t last = 0;
    for (uint32_t i = 0; i < table[0]; i++) {
        if (buckets[i] && buckets[i] < table[1])
            return 0;
        if (buckets[i] > last)
            last = buckets[i];
    }
    if (!last)
        return table[1];
    while (last < limit) {
        size_t offset = bytes + ((size_t)last - table[1]) * 4;
        if (offset > UINTPTR_MAX - gnu_hash)
            return 0;
        const uint32_t *chain = dynamic_pointer(library, gnu_hash + offset, 4);
        if (!chain)
            return 0;
        last++;
        if (*chain & 1)
            return last;
    }
    return 0;
}

static bool function_symbol(module_library_t *library, const void *address, const char *name) {
    if (!address || !name || !library->dynamic_count || library->dynamic_count > 1048576
        || (uintptr_t)library->dynamic % _Alignof(ElfW(Dyn))
        || !image_range(library,
                        library->dynamic,
                        library->dynamic_count * sizeof(ElfW(Dyn)),
                        false))
        return false;
    uintptr_t symbols = 0, strings = 0, hash = 0, gnu_hash = 0;
    size_t entry_size = 0, string_size = 0;
    unsigned seen = 0;
    bool terminated = false;
    for (size_t i = 0; i < library->dynamic_count; i++) {
        const ElfW(Dyn) *entry = library->dynamic + i;
        if (entry->d_tag == DT_NULL) {
            terminated = true;
            break;
        }
        unsigned field = 0;
        switch (entry->d_tag) {
            case DT_SYMTAB:
                field = 1;
                symbols = entry->d_un.d_ptr;
                break;
            case DT_SYMENT:
                field = 2;
                entry_size = entry->d_un.d_val;
                break;
            case DT_STRTAB:
                field = 4;
                strings = entry->d_un.d_ptr;
                break;
            case DT_STRSZ:
                field = 8;
                string_size = entry->d_un.d_val;
                break;
            case DT_HASH:
                field = 16;
                hash = entry->d_un.d_ptr;
                break;
            case DT_GNU_HASH:
                field = 32;
                gnu_hash = entry->d_un.d_ptr;
                break;
            default:
                break;
        }
        if (seen & field)
            return false;
        seen |= field;
    }
    if (!terminated || !symbols || !strings || !string_size || entry_size != sizeof(ElfW(Sym)))
        return false;
    size_t count = dynamic_symbol_count(library, hash, gnu_hash);
    const ElfW(Sym) *table = dynamic_pointer(library, symbols, count * sizeof(ElfW(Sym)));
    const char *names = dynamic_pointer(library, strings, string_size);
    if (!count || !table || (uintptr_t)table % _Alignof(ElfW(Sym)) || !names)
        return false;
    bool found = false;
    for (size_t i = 0; i < count; i++) {
        const ElfW(Sym) *symbol = table + i;
        unsigned binding = ELF64_ST_BIND(symbol->st_info);
        unsigned visibility = ELF64_ST_VISIBILITY(symbol->st_other);
        if (symbol->st_shndx == SHN_UNDEF || (binding != STB_GLOBAL && binding != STB_WEAK)
            || (visibility != STV_DEFAULT && visibility != STV_PROTECTED))
            continue;
        if (symbol->st_name >= string_size
            || !memchr(names + symbol->st_name, 0, string_size - symbol->st_name))
            return false;
        if (strcmp(names + symbol->st_name, name))
            continue;
        /* Ambiguous/versioned same-name definitions are not generated by the
         * supported module compiler. Never let a FUNC hide an OBJECT alias. */
        if (found || symbol->st_shndx == SHN_ABS || ELF64_ST_TYPE(symbol->st_info) != STT_FUNC
            || symbol->st_value > UINTPTR_MAX - library->base
            || library->base + symbol->st_value != (uintptr_t)address)
            return false;
        found = true;
    }
    return found;
}

#elif defined(_WIN32)
static bool image_range(module_library_t *library, const void *pointer, size_t length, bool code) {
    return module_windows_image_range(library->handle, pointer, length, code);
}

static void *symbol_address(module_library_t *library, const char *symbol) {
    return module_windows_symbol_address(library->handle, symbol);
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

bool release_module_library_checked(module_library_t *library, char **diagnostic) {
    if (!library)
        return true;
    for (size_t i = 0; i < library->count; i++)
        FREE(library->symbols[i].symbol);
    FREE(library->symbols);
#ifdef __linux__
    FREE(library->ranges);
#endif
    bool ok = !library->handle || close_native_library_handle_checked(library->handle, diagnostic);
    FREE(library);
    return ok;
}

void release_module_library(module_library_t *library) {
    (void)release_module_library_checked(library, NULL);
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

module_library_result_t load_module_library_observed(const char *path,
                                                     const yyjson_doc *metadata,
                                                     size_t variant_index,
                                                     module_library_observer_t observer,
                                                     void *context) {
    module_library_result_t result = {0};
    const yyjson_val *root = metadata ? yyjson_doc_get_root((yyjson_doc *)metadata) : NULL;
    const yyjson_val *variant = yyjson_arr_get(yyjson_obj_get(root, "native"), variant_index);
    if (!path || !variant || goat_module_abi_validate(variant) != GOAT_ABI_OK) {
        result.diagnostic = copy_native_library_diagnostic("Invalid module-v1 loader arguments");
        return result;
    }
    module_library_t *library = CALLOC(sizeof(*library));
    if (observer)
        observer(MODULE_LIBRARY_LOAD, false, context);
    library->handle = open_native_library_handle(path, &result.diagnostic);
    const char *failure = NULL;
    if (!library->handle)
        goto failed;
    if (observer) {
        observer(MODULE_LIBRARY_LOAD, true, context);
        observer(MODULE_LIBRARY_DESCRIPTOR, false, context);
    }
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
    if (observer) {
        observer(MODULE_LIBRARY_DESCRIPTOR, true, context);
        observer(MODULE_LIBRARY_SYMBOLS, false, context);
    }
    const yyjson_val *specs = yyjson_obj_get(variant, "specializations");
    size_t count = yyjson_arr_size(specs);
    library->symbols = CALLOC(allocation_size_multiply(count, sizeof(module_symbol_t)));
    for (size_t i = 0; i < count; i++) {
        const char *name = yyjson_get_str(yyjson_obj_get(yyjson_arr_get(specs, i), "symbol"));
        void *address = name ? symbol_address(library, name) : NULL;
        bool callable = image_range(library, address, 1, true);
#ifdef __linux__
        callable = callable && function_symbol(library, address, name);
#endif
        if (!callable) {
            failure = "Native module adapter symbol is absent, foreign or not executable";
            goto failed;
        }
        module_symbol_t *symbol = library->symbols + library->count++;
        symbol->symbol = copy_native_library_diagnostic(name);
        _Static_assert(sizeof(symbol->adapter) == sizeof(address),
                       "Adapter pointer representation");
        memcpy(&symbol->adapter, &address, sizeof(address));
    }
    if (observer)
        observer(MODULE_LIBRARY_SYMBOLS, true, context);
    result.valid = true;
    result.library = library;
    return result;
failed:
    if (failure) {
        FREE(result.diagnostic);
        result.diagnostic = copy_native_library_diagnostic(failure);
    }
    result.library = library;
    return result;
}

module_library_result_t
load_module_library(const char *path, const yyjson_doc *metadata, size_t variant_index) {
    module_library_result_t result =
        load_module_library_observed(path, metadata, variant_index, NULL, NULL);
    if (!result.valid) {
        release_module_library(result.library);
        result.library = NULL;
    }
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
