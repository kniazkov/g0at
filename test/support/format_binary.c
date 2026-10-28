/** @file format_binary.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent byte-level specification oracle, ported from reference.py.
 */
#include "format_binary.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(c, message)                                                                        \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            if (error && error_size)                                                               \
                snprintf(error, error_size, "%s", message);                                        \
            return false;                                                                          \
        }                                                                                          \
    } while (0)
#define GET(v, k) yyjson_mut_obj_get((v), (k))

static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put(unsigned char *p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (unsigned char)(n >> (8 * i));
}

static void words(unsigned char *p, const uint32_t *v, size_t n) {
    for (size_t i = 0; i < n; ++i)
        put(p + 4 * i, v[i]);
}

unsigned char *format_binary_minimal(size_t *size) {
    unsigned char *p = calloc(112, 1);
    if (!p)
        return NULL;
    const uint32_t h[] = {1, 64, 112, 1, 0, 1, 0, 1, 64, 72, 72, 88, 88, 112};
    const uint32_t f[] = {0, 1, 0, 0}, l[] = {0, 1, 0, 1, 1, 0};
    memcpy(p, "GOATBC01", 8);
    words(p + 8, h, 14);
    p[64] = 2;
    words(p + 72, f, 4);
    words(p + 88, l, 6);
    *size = 112;
    return p;
}

unsigned char *format_binary_importing(size_t *size) {
    unsigned char *p = calloc(252, 1);
    if (!p)
        return NULL;
    const uint32_t h[] = {1, 64, 252, 5, 2, 1, 0, 1, 64, 104, 136, 152, 152, 176};
    const uint32_t d[] = {0, 36, 1, 8, 40, 36, 1, 8}, f[] = {0, 5, 0, 0}, l[] = {0, 5, 0, 1, 1, 0};
    const unsigned char ops[] = {74, 5, 74, 5, 2};
    memcpy(p, "GOATBC01", 8);
    words(p + 8, h, 14);
    for (size_t i = 0; i < 5; ++i)
        p[64 + 8 * i] = ops[i];
    put(p + 84, 1);
    words(p + 104, d, 8);
    words(p + 136, f, 4);
    words(p + 152, l, 6);
    const char *names[] = {"math@1.0", "math@2.0"};
    for (size_t i = 0; i < 2; ++i)
        for (size_t j = 0; j < 8; ++j)
            put(p + 176 + 40 * i + 4 * j, (unsigned char)names[i][j]);
    *size = 252;
    return p;
}

bool format_binary_check(const unsigned char *data, size_t size, char *error, size_t error_size) {
    REQUIRE(data && size >= 64, "short bytecode");
    uint32_t h[14];
    for (size_t i = 0; i < 14; ++i)
        h[i] = u32(data + 8 + 4 * i);
    uint32_t ni = h[3], nd = h[4], nf = h[5], nn = h[6], nl = h[7];
    uint32_t oi = h[8], od = h[9], of = h[10], on = h[11], ol = h[12], payload = h[13];
    REQUIRE(!memcmp(data, "GOATBC01", 8) && h[0] == 1 && h[1] == 64 && h[2] == size,
            "bytecode header");
    REQUIRE(ni > 0 && nf > 0, "empty code/files");
    REQUIRE(oi == 64 && od == (uint64_t)oi + 8ull * ni && of == (uint64_t)od + 16ull * nd
                && on == (uint64_t)of + 16ull * nf && ol == (uint64_t)on + 32ull * nn
                && payload == (uint64_t)ol + 24ull * nl && payload <= size,
            "section layout");
    REQUIRE(payload % 8 == 0, "payload alignment");
    for (uint32_t i = 0; i < ni; ++i) {
        const unsigned char *p = data + oi + 8 * (size_t)i;
        unsigned op = p[0], a = p[2] | (unsigned)p[3] << 8;
        uint32_t b = u32(p + 4);
        REQUIRE(op <= 76 && p[1] == 0, "opcode/flags");
        if (op == 2 || op == 5 || op == 75 || op == 76)
            REQUIRE(a == 0 && b == 0, "unused operands");
        if (op == 3 || op == 4 || op == 28 || op == 29 || op == 54 || op == 61 || op == 65
            || op == 66 || op == 67)
            REQUIRE(b < ni, "jump range");
        if (op == 11 || op == 12 || op == 47)
            REQUIRE(i > 0 && data[oi + 8 * (size_t)(i - 1)] == 1, "missing ARG");
    }
    uint64_t end = 0;
    for (uint32_t i = 0; i < nf; ++i) {
        const unsigned char *p = data + of + 16 * (size_t)i;
        uint32_t first = u32(p), count = u32(p + 4), entry = u32(p + 8);
        REQUIRE(first == end && count > 0 && entry >= first && entry < (uint64_t)first + count
                    && (uint64_t)first + count <= ni && u32(p + 12) == 0,
                "file span");
        end = (uint64_t)first + count;
    }
    REQUIRE(end == ni, "file coverage");
    end = 0;
    for (uint32_t i = 0; i < nl; ++i) {
        const unsigned char *p = data + ol + 24 * (size_t)i;
        uint32_t first = u32(p), count = u32(p + 4);
        REQUIRE(first == end && count > 0 && (uint64_t)first + count <= ni && u32(p + 8) < nf,
                "location range");
        REQUIRE((u32(p + 12) == 0) == (u32(p + 16) == 0), "synthetic location");
        end = (uint64_t)first + count;
    }
    REQUIRE(end == ni, "location coverage");
    end = 0;
    for (uint32_t i = 0; i < nd; ++i) {
        const unsigned char *p = data + od + 16 * (size_t)i;
        uint32_t offset = u32(p), length = u32(p + 4), kind = u32(p + 8), count = u32(p + 12);
        REQUIRE(offset % 8 == 0 && offset <= size - payload && length <= size - payload - offset,
                "data bounds");
        REQUIRE(offset == (end + 7) / 8 * 8, "data layout/padding");
        for (uint64_t j = end; j < offset; ++j)
            REQUIRE(data[payload + (size_t)j] == 0, "data layout/padding");
        const unsigned char *raw = data + payload + offset;
        REQUIRE(kind >= 1 && kind <= 3, "data kind");
        if (kind == 1) {
            REQUIRE(length == 4ull * ((uint64_t)count + 1) && u32(raw + length - 4) == 0,
                    "string size/terminator");
            for (uint32_t j = 0; j < count; ++j) {
                uint32_t cp = u32(raw + 4 * (size_t)j);
                REQUIRE(cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff), "invalid UTF-32 scalar");
            }
        } else if (kind == 2)
            REQUIRE(length == 4ull * count, "vector size");
        else
            REQUIRE(length == 16 && count == 1, "declaration size");
        end = (uint64_t)offset + length;
    }
    REQUIRE(payload + end == size, "unowned trailing bytes");
    return true;
}

/* FIPS 180-4 SHA-256, implemented here rather than calling the tested Goat hash. */
static uint32_t rotate(uint32_t x, unsigned n) {
    return x >> n | x << (32 - n);
}

static void sha_block(uint32_t state[8], const unsigned char block[64]) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    uint32_t w[64];
    for (unsigned i = 0; i < 16; ++i)
        w[i] = (uint32_t)block[4 * i] << 24 | (uint32_t)block[4 * i + 1] << 16
               | (uint32_t)block[4 * i + 2] << 8 | block[4 * i + 3];
    for (unsigned i = 16; i < 64; ++i) {
        uint32_t x = w[i - 15], y = w[i - 2];
        w[i] = w[i - 16] + (rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3)) + w[i - 7]
               + (rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10));
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5],
             g = state[6], h = state[7];
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t t =
            h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
        uint32_t u = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t;
        d = c;
        c = b;
        b = a;
        a = t + u;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void format_binary_sha256(const void *data, size_t size, char hex[65]) {
    uint32_t state[] = {0x6a09e667,
                        0xbb67ae85,
                        0x3c6ef372,
                        0xa54ff53a,
                        0x510e527f,
                        0x9b05688c,
                        0x1f83d9ab,
                        0x5be0cd19};
    const unsigned char *p = data;
    size_t left = size;
    while (left >= 64) {
        sha_block(state, p);
        p += 64;
        left -= 64;
    }
    unsigned char last[128] = {0};
    if (left)
        memcpy(last, p, left);
    last[left] = 0x80;
    size_t blocks = left < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)size * 8;
    for (unsigned i = 0; i < 8; ++i)
        last[blocks - 1 - i] = (unsigned char)(bits >> (8 * i));
    sha_block(state, last);
    if (blocks == 128)
        sha_block(state, last + 64);
    const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) {
        unsigned char v = (unsigned char)(state[i / 4] >> (24 - 8 * (i % 4)));
        hex[2 * i] = digits[v >> 4];
        hex[2 * i + 1] = digits[v & 15];
    }
    hex[64] = 0;
}

bool format_binary_main_hash(yyjson_mut_val *metadata,
                             yyjson_mut_val *source,
                             const unsigned char *data,
                             size_t size,
                             char hex[65],
                             char *error,
                             size_t error_size) {
    size_t n;
    unsigned char *input =
        format_metadata_hash_input(metadata, source, data, size, &n, error, error_size);
    if (!input)
        return false;
    format_binary_sha256(input, n, hex);
    free(input);
    return true;
}

/* Compare an already-validated UTF-32 string to a UTF-8 JSON string, with no
 * locale, wchar_t-width or production string helper dependency. */
static bool utf32_equals(const unsigned char *raw, size_t count, const char *text, size_t length) {
    size_t pos = 0;
    for (size_t i = 0; i < count; ++i) {
        uint32_t cp = u32(raw + 4 * i);
        unsigned char bytes[4];
        size_t n;
        if (cp < 0x80) {
            bytes[0] = (unsigned char)cp;
            n = 1;
        } else if (cp < 0x800) {
            bytes[0] = (unsigned char)(0xc0 | (cp >> 6));
            bytes[1] = (unsigned char)(0x80 | (cp & 63));
            n = 2;
        } else if (cp < 0x10000) {
            bytes[0] = (unsigned char)(0xe0 | (cp >> 12));
            bytes[1] = (unsigned char)(0x80 | ((cp >> 6) & 63));
            bytes[2] = (unsigned char)(0x80 | (cp & 63));
            n = 3;
        } else {
            bytes[0] = (unsigned char)(0xf0 | (cp >> 18));
            bytes[1] = (unsigned char)(0x80 | ((cp >> 12) & 63));
            bytes[2] = (unsigned char)(0x80 | ((cp >> 6) & 63));
            bytes[3] = (unsigned char)(0x80 | (cp & 63));
            n = 4;
        }
        if (n > length - pos || memcmp(bytes, text + pos, n))
            return false;
        pos += n;
    }
    return pos == length;
}

static bool check_parts(yyjson_mut_val *v,
                        yyjson_mut_val *source,
                        const unsigned char *data,
                        size_t size,
                        char *error,
                        size_t error_size) {
    uint32_t ni = u32(data + 20), nd = u32(data + 24), nf = u32(data + 28), nn = u32(data + 32);
    uint32_t oi = u32(data + 40), od = u32(data + 44), of = u32(data + 48), on = u32(data + 52),
             payload = u32(data + 60);
    yyjson_mut_val *files = GET(v, "files"), *functions = GET(v, "functions");
    REQUIRE(yyjson_mut_arr_size(files) == nf && yyjson_mut_arr_size(functions) == nn,
            "binary/JSON table counts");
    for (uint32_t i = 0; i < nf; ++i)
        REQUIRE(u32(data + of + 16 * (size_t)i + 8)
                    == yyjson_mut_get_uint(GET(yyjson_mut_arr_get(files, i), "entry")),
                "binary/JSON file entry");
    for (uint32_t i = 0; i < nn; ++i) {
        const unsigned char *p = data + on + 32 * (size_t)i;
        yyjson_mut_val *f = yyjson_mut_arr_get(functions, i);
        uint32_t ins = u32(p + 8);
        REQUIRE(u32(p) == yyjson_mut_get_uint(GET(f, "functionId"))
                    && u32(p + 4) == yyjson_mut_get_uint(GET(f, "file"))
                    && ins == yyjson_mut_get_uint(GET(f, "instruction"))
                    && u32(p + 20) == yyjson_mut_arr_size(GET(f, "parameters")),
                "binary/JSON function");
        REQUIRE(ins < ni && data[oi + 8 * (size_t)ins] == 47 && u32(p + 12) < ni
                    && u32(p + 28) == 0,
                "function record");
    }
    for (uint32_t i = 0; i < ni; ++i) {
        const unsigned char *p = data + oi + 8 * (size_t)i;
        uint32_t b = u32(p + 4);
        if (*p == 73)
            REQUIRE(b < nf, "include file");
        if (*p == 74) {
            REQUIRE(b < nd, "import descriptor");
            const unsigned char *d = data + od + 16 * (size_t)b;
            REQUIRE(u32(d + 8) == 1, "import string");
            const unsigned char *raw = data + payload + u32(d);
            uint32_t count = u32(d + 12);
            size_t at = 0, ats = 0;
            for (uint32_t j = 0; j < count; ++j)
                if (u32(raw + 4 * (size_t)j) == '@') {
                    at = j;
                    ++ats;
                }
            REQUIRE(ats == 1, "qualified import");
            bool found = false;
            yyjson_mut_val *deps = GET(v, "dependencies"), *dep;
            size_t j, n;
            yyjson_mut_arr_foreach(deps, j, n, dep) {
                yyjson_mut_val *name = GET(dep, "name"), *version = GET(dep, "version");
                if (utf32_equals(raw, at, yyjson_mut_get_str(name), yyjson_mut_get_len(name))
                    && utf32_equals(raw + 4 * (at + 1),
                                    count - at - 1,
                                    yyjson_mut_get_str(version),
                                    yyjson_mut_get_len(version)))
                    found = true;
            }
            REQUIRE(found, "undeclared import");
        }
    }
    char hash[65];
    if (!format_binary_main_hash(v, source, data, size, hash, error, error_size))
        return false;
    yyjson_mut_val *expected = GET(v, "mainHash");
    REQUIRE(yyjson_mut_get_len(expected) == 64 && !memcmp(hash, yyjson_mut_get_str(expected), 64),
            "main hash mismatch");
    return true;
}

bool format_binary_check_module(yyjson_mut_val *metadata,
                                yyjson_mut_val *source,
                                const unsigned char *data,
                                size_t size,
                                char *error,
                                size_t error_size) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    REQUIRE(doc, "allocation failure");
    yyjson_mut_val *v = yyjson_mut_val_mut_copy(doc, metadata);
    bool ok = v && format_metadata_normalize(doc, v, "module", source, error, error_size)
              && format_binary_check(data, size, error, error_size)
              && check_parts(v, source, data, size, error, error_size);
    yyjson_mut_doc_free(doc);
    return ok;
}
