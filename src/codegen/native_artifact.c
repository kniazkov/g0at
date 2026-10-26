/** @file native_artifact.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Bounded static ELF/PE export lookup; no OS loader.
 */
#include "native_artifact.h"

#include "lib/allocate.h"
#include "lib/binary_file.h"

#include <stdint.h>
#include <string.h>

struct native_artifact_t {
    unsigned char *data;
    size_t size;
    bool elf, wide;
    uint64_t sections, section_count, section_stride;
    uint64_t exports, export_rva, export_size;
};

typedef struct {
    uint64_t offset, size;
    bool function;
} symbol_t;

static bool range(const native_artifact_t *a, uint64_t offset, uint64_t size) {
    return offset <= a->size && size <= a->size - offset;
}

static uint64_t read_number(const native_artifact_t *a, uint64_t offset, unsigned width) {
    if (!range(a, offset, width))
        return 0;
    uint64_t value = 0;
    for (unsigned i = 0; i < width; i++)
        value |= (uint64_t)a->data[offset + i] << (8 * i);
    return value;
}

static const char *read_string(const native_artifact_t *a, uint64_t offset, uint64_t size) {
    if (!range(a, offset, size) || !size)
        return NULL;
    const char *text = (const char *)a->data + offset;
    return memchr(text, 0, (size_t)size) ? text : NULL;
}

static bool error(char **diagnostic, const char *text) {
    if (diagnostic) {
        size_t size = strlen(text) + 1;
        *diagnostic = ALLOC(size);
        memcpy(*diagnostic, text, size);
    }
    return false;
}

static bool pe_region(const native_artifact_t *a, uint64_t rva, uint64_t length, symbol_t *out) {
    for (uint64_t i = 0; i < a->section_count; i++) {
        uint64_t s = a->sections + i * 40;
        uint64_t address = read_number(a, s + 12, 4), size = read_number(a, s + 16, 4);
        uint64_t offset = read_number(a, s + 20, 4);
        if (rva >= address && rva - address <= size && length <= size - (rva - address)
            && range(a, offset + rva - address, size - (rva - address))) {
            *out = (symbol_t){.offset = offset + rva - address,
                              .size = size - (rva - address),
                              .function = (read_number(a, s + 36, 4) & 0x20000000) != 0};
            return true;
        }
    }
    return false;
}

static bool lookup_pe(const native_artifact_t *a, const char *name, symbol_t *out) {
    uint64_t nfunc = read_number(a, a->exports + 20, 4);
    uint64_t nnames = read_number(a, a->exports + 24, 4);
    symbol_t functions, names, ordinals;
    if (!pe_region(a, read_number(a, a->exports + 28, 4), nfunc * 4, &functions)
        || !pe_region(a, read_number(a, a->exports + 32, 4), nnames * 4, &names)
        || !pe_region(a, read_number(a, a->exports + 36, 4), nnames * 2, &ordinals))
        return false;
    bool found = false;
    for (uint64_t i = 0; i < nnames; i++) {
        symbol_t string;
        if (!pe_region(a, read_number(a, names.offset + i * 4, 4), 1, &string))
            return false;
        const char *actual = read_string(a, string.offset, string.size);
        uint64_t ordinal = read_number(a, ordinals.offset + i * 2, 2);
        if (!actual || ordinal >= nfunc)
            return false;
        if (strcmp(actual, name))
            continue;
        uint64_t address = read_number(a, functions.offset + ordinal * 4, 4);
        if (found || !address
            || (address >= a->export_rva && address - a->export_rva < a->export_size)
            || !pe_region(a, address, 1, out))
            return false;
        found = true;
    }
    return found;
}

static bool lookup_elf(const native_artifact_t *a, const char *name, symbol_t *out) {
    bool found = false;
    unsigned word = a->wide ? 8 : 4;
    for (uint64_t i = 0; i < a->section_count; i++) {
        uint64_t s = a->sections + i * a->section_stride;
        if (read_number(a, s + 4, 4) != 11)
            continue;
        uint64_t offset = read_number(a, s + (a->wide ? 24 : 16), word);
        uint64_t size = read_number(a, s + (a->wide ? 32 : 20), word);
        uint64_t link = read_number(a, s + (a->wide ? 40 : 24), 4);
        uint64_t stride = read_number(a, s + (a->wide ? 56 : 36), word);
        if (stride < (a->wide ? 24u : 16u) || size % stride || !range(a, offset, size)
            || link >= a->section_count)
            return false;
        uint64_t strings = a->sections + link * a->section_stride;
        uint64_t base = read_number(a, strings + (a->wide ? 24 : 16), word);
        uint64_t length = read_number(a, strings + (a->wide ? 32 : 20), word);
        if (!range(a, base, length))
            return false;
        for (uint64_t p = offset; p < offset + size; p += stride) {
            uint64_t index = read_number(a, p, 4);
            const char *actual =
                index < length ? read_string(a, base + index, length - index) : NULL;
            if (!actual)
                return false;
            if (strcmp(actual, name))
                continue;
            uint64_t info = read_number(a, p + (a->wide ? 4 : 12), 1);
            uint64_t visibility = read_number(a, p + (a->wide ? 5 : 13), 1) & 3;
            uint64_t section = read_number(a, p + (a->wide ? 6 : 14), 2);
            uint64_t address = read_number(a, p + (a->wide ? 8 : 4), word);
            uint64_t bytes = read_number(a, p + (a->wide ? 16 : 8), word);
            if (found || (info >> 4 != 1 && info >> 4 != 2) || (visibility != 0 && visibility != 3)
                || !section || section >= a->section_count || !bytes
                || ((info & 15) != 1 && (info & 15) != 2))
                return false;
            uint64_t target = a->sections + section * a->section_stride;
            if (read_number(a, target + 4, 4) == 8)
                return false;
            uint64_t start = read_number(a, target + (a->wide ? 16 : 12), word);
            uint64_t file = read_number(a, target + (a->wide ? 24 : 16), word);
            uint64_t available = read_number(a, target + (a->wide ? 32 : 20), word);
            if (address < start || address - start > available
                || bytes > available - (address - start) || !range(a, file, available))
                return false;
            uint64_t flags = read_number(a, target + 8, word);
            bool function = (info & 15) == 2;
            if (!(flags & 2) || (function && !(flags & 4)))
                return false;
            *out =
                (symbol_t){.offset = file + address - start, .size = bytes, .function = function};
            found = true;
        }
    }
    return found;
}

static native_artifact_t *parse_artifact(native_artifact_t *a, char **diagnostic) {
    bool valid = false;
    if (a->data && a->size >= 64 && !memcmp(a->data, "\177ELF", 4)) {
        a->elf = true;
        a->wide = a->data[4] == 2;
        unsigned word = a->wide ? 8 : 4;
        a->sections = read_number(a, a->wide ? 40 : 32, word);
        a->section_count = read_number(a, a->wide ? 60 : 48, 2);
        a->section_stride = read_number(a, a->wide ? 58 : 46, 2);
        valid = (a->wide || a->data[4] == 1) && a->data[5] == 1 && a->data[6] == 1
                && read_number(a, 16, 2) == 3 && a->section_count
                && a->section_stride >= (a->wide ? 64u : 40u)
                && range(a, a->sections, a->section_count * a->section_stride);
    } else if (a->data && a->size >= 64 && !memcmp(a->data, "MZ", 2)) {
        uint64_t pe = read_number(a, 60, 4);
        uint64_t optional = pe + 24, length = read_number(a, pe + 20, 2);
        a->wide = read_number(a, optional, 2) == 0x20b;
        uint64_t dirs = optional + (a->wide ? 112 : 96);
        a->sections = optional + length;
        a->section_count = read_number(a, pe + 6, 2);
        a->section_stride = 40;
        a->export_rva = read_number(a, dirs, 4);
        a->export_size = read_number(a, dirs + 4, 4);
        symbol_t region;
        valid = range(a, pe, 24) && !memcmp(a->data + pe, "PE\0\0", 4)
                && (a->wide || read_number(a, optional, 2) == 0x10b)
                && length >= (a->wide ? 120u : 104u) && range(a, optional, length)
                && read_number(a, optional + (a->wide ? 108 : 92), 4) >= 1
                && range(a, a->sections, a->section_count * 40) && a->section_count
                && (read_number(a, pe + 22, 2) & 0x2000) && a->export_size >= 40
                && pe_region(a, a->export_rva, a->export_size, &region);
        if (valid)
            a->exports = region.offset;
    }
    if (!valid) {
        error(diagnostic, "Native image export metadata is unavailable or malformed");
        close_native_artifact(a);
        return NULL;
    }
    return a;
}

native_artifact_t *open_native_artifact(const char *path, char **diagnostic) {
    if (diagnostic)
        *diagnostic = NULL;
    if (!path || !diagnostic)
        return NULL;
    native_artifact_t *a = CALLOC(sizeof(*a));
    a->data = read_binary_file(path, 128u * 1024u * 1024u, &a->size);
    return parse_artifact(a, diagnostic);
}

native_artifact_t *open_native_artifact_bytes(const void *bytes, size_t size, char **diagnostic) {
    if (diagnostic)
        *diagnostic = NULL;
    if (!diagnostic)
        return NULL;
    if (!bytes || !size || size > 128u * 1024u * 1024u) {
        error(diagnostic, "Native image snapshot has an invalid size");
        return NULL;
    }
    native_artifact_t *a = CALLOC(sizeof(*a));
    a->data = ALLOC(size);
    a->size = size;
    memcpy(a->data, bytes, size);
    return parse_artifact(a, diagnostic);
}

void close_native_artifact(native_artifact_t *artifact) {
    if (!artifact)
        return;
    FREE(artifact->data);
    FREE(artifact);
}

bool native_artifact_descriptor(const native_artifact_t *artifact,
                                const void **bytes,
                                size_t *size,
                                char **diagnostic) {
    if (bytes)
        *bytes = NULL;
    if (size)
        *size = 0;
    if (diagnostic)
        *diagnostic = NULL;
    if (!artifact || !bytes || !size || !diagnostic)
        return false;
    symbol_t symbol;
    bool found = artifact->elf ? lookup_elf(artifact, "goat_module_descriptor_v1", &symbol)
                               : lookup_pe(artifact, "goat_module_descriptor_v1", &symbol);
    if (!found || symbol.function || symbol.size < 17
        || memcmp(artifact->data + symbol.offset, "GOATND01", 8))
        return error(diagnostic, "Native descriptor export is missing or not file-backed data");
    uint64_t length = read_number(artifact, symbol.offset + 8, 8);
    if (length > symbol.size - 17 || (artifact->elf && length != symbol.size - 17)
        || artifact->data[symbol.offset + 16 + length])
        return error(diagnostic, "Native descriptor envelope is truncated or malformed");
    *bytes = artifact->data + symbol.offset;
    *size = (size_t)length + 17;
    return true;
}

bool native_artifact_has_function(const native_artifact_t *artifact, const char *symbol) {
    if (!artifact || !symbol || !*symbol)
        return false;
    symbol_t found;
    bool ok =
        artifact->elf ? lookup_elf(artifact, symbol, &found) : lookup_pe(artifact, symbol, &found);
    return ok && found.function;
}
