/** @file format_zip.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent ZIP fixtures and reader. No production ZIP API is used.
 */
#include "format_zip.h"

#include "third_party/zlib/zlib.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char *p;
    size_t n, cap;
} buffer_t;

static bool fail(char *e, size_t n, const char *s) {
    if (e && n)
        snprintf(e, n, "%s", s);
    return false;
}

static bool append(buffer_t *b, const void *p, size_t n) {
    if (n > SIZE_MAX - b->n || (n && !p))
        return false;
    size_t wanted = b->n + n;
    if (wanted > b->cap) {
        size_t cap = wanted < 256 ? 256 : wanted;
        unsigned char *q = realloc(b->p, cap);
        if (!q)
            return false;
        b->p = q;
        b->cap = cap;
    }
    if (n)
        memcpy(b->p + b->n, p, n);
    b->n = wanted;
    return true;
}

static uint32_t get(const unsigned char *p, unsigned n) {
    uint32_t v = 0;
    for (unsigned i = 0; i < n; i++)
        v |= (uint32_t)p[i] << (8 * i);
    return v;
}

static bool put(buffer_t *b, uint32_t v, unsigned n) {
    unsigned char p[4];
    for (unsigned i = 0; i < n; i++)
        p[i] = (unsigned char)(v >> (8 * i));
    return append(b, p, n);
}

bool format_bytes_patch_le(unsigned char *p, size_t n, size_t off, uint64_t v, unsigned width) {
    if (!p || (width != 1 && width != 2 && width != 4 && width != 8) || off > n || width > n - off)
        return false;
    for (unsigned i = 0; i < width; i++)
        p[off + i] = (unsigned char)(v >> (8 * i));
    return true;
}

void format_zip_entry_init(format_zip_entry_t *e,
                           const char *name,
                           const void *data,
                           size_t size,
                           bool compressed) {
    memset(e, 0, sizeof(*e));
    e->name = (const unsigned char *)name;
    e->name_size = strlen(name);
    e->data = data;
    e->data_size = size;
    e->method = compressed ? 8 : 0;
    e->flags = 0x800;
    e->date = 33;
    e->made_by = 0x314;
    e->external = 0100644u << 16;
}

unsigned char *format_zip_build(const format_zip_entry_t *entries,
                                size_t count,
                                const void *comment,
                                size_t comment_size,
                                bool streamed,
                                size_t *size,
                                char *error,
                                size_t error_size) {
    buffer_t out = {0}, cd = {0};
    unsigned char *packed = NULL;
    const char *why = "ZIP fixture allocation/size";
    if (size)
        *size = 0;
    if (count > 65535 || comment_size > 65535 || (count && !entries) || (comment_size && !comment))
        goto bad;
    for (size_t i = 0; i < count; i++) {
        const format_zip_entry_t *e = &entries[i];
        size_t pn = e->data_size;
        if (e->name_size > 65535 || e->extra_size > 65535 || e->comment_size > 65535
            || e->data_size > UINT_MAX || out.n > UINT32_MAX || (e->data_size && !e->data))
            goto bad;
        const unsigned char *payload = e->data;
        if (e->method == 8) {
            z_stream z;
            memset(&z, 0, sizeof(z));
            if (deflateInit2(&z, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
                goto bad;
            uLong bound = deflateBound(&z, (uLong)e->data_size);
            if (bound > UINT_MAX) {
                deflateEnd(&z);
                goto bad;
            }
            packed = malloc(bound ? bound : 1);
            if (!packed) {
                deflateEnd(&z);
                goto bad;
            }
            z.next_in = (Bytef *)e->data;
            z.avail_in = (uInt)e->data_size;
            z.next_out = packed;
            z.avail_out = (uInt)bound;
            int rc = deflate(&z, Z_FINISH);
            pn = (size_t)z.total_out;
            deflateEnd(&z);
            if (rc != Z_STREAM_END)
                goto bad;
            payload = packed;
        } else if (e->method != 0) {
            why = "unsupported fixture compression";
            goto bad;
        }
        uint32_t crc = (uint32_t)crc32(0, e->data, (uInt)e->data_size), off = (uint32_t)out.n;
        uint16_t flags = e->flags | (streamed ? 8 : 0);
#define P(b, v, n)                                                                                 \
    do {                                                                                           \
        if (!put(&(b), (uint32_t)(v), (n)))                                                        \
            goto bad;                                                                              \
    } while (0)
#define A(b, p, n)                                                                                 \
    do {                                                                                           \
        if (!append(&(b), (p), (n)))                                                               \
            goto bad;                                                                              \
    } while (0)
        P(out, 0x04034b50, 4);
        P(out, 20, 2);
        P(out, flags, 2);
        P(out, e->method, 2);
        P(out, e->time, 2);
        P(out, e->date, 2);
        P(out, streamed ? 0 : crc, 4);
        P(out, streamed ? 0 : pn, 4);
        P(out, streamed ? 0 : e->data_size, 4);
        P(out, e->name_size, 2);
        P(out, e->extra_size, 2);
        A(out, e->name, e->name_size);
        A(out, e->extra, e->extra_size);
        A(out, payload, pn);
        if (streamed) {
            P(out, 0x08074b50, 4);
            P(out, crc, 4);
            P(out, pn, 4);
            P(out, e->data_size, 4);
        }
        P(cd, 0x02014b50, 4);
        P(cd, e->made_by, 2);
        P(cd, 20, 2);
        P(cd, flags, 2);
        P(cd, e->method, 2);
        P(cd, e->time, 2);
        P(cd, e->date, 2);
        P(cd, crc, 4);
        P(cd, pn, 4);
        P(cd, e->data_size, 4);
        P(cd, e->name_size, 2);
        P(cd, e->extra_size, 2);
        P(cd, e->comment_size, 2);
        P(cd, 0, 2);
        P(cd, e->internal, 2);
        P(cd, e->external, 4);
        P(cd, off, 4);
        A(cd, e->name, e->name_size);
        A(cd, e->extra, e->extra_size);
        A(cd, e->comment, e->comment_size);
        free(packed);
        packed = NULL;
    }
    if (out.n > UINT32_MAX || cd.n > UINT32_MAX)
        goto bad;
    {
        uint32_t off = (uint32_t)out.n;
        A(out, cd.p, cd.n);
        P(out, 0x06054b50, 4);
        P(out, 0, 2);
        P(out, 0, 2);
        P(out, count, 2);
        P(out, count, 2);
        P(out, cd.n, 4);
        P(out, off, 4);
        P(out, comment_size, 2);
        A(out, comment, comment_size);
    }
    free(cd.p);
    if (size)
        *size = out.n;
    return out.p;
bad:
    free(out.p);
    free(cd.p);
    free(packed);
    fail(error, error_size, why);
    return NULL;
#undef P
#undef A
}

static bool range(size_t at, size_t n, size_t end) {
    return at <= end && n <= end - at;
}

bool format_zip_inspect(const unsigned char *data,
                        size_t size,
                        size_t index,
                        format_zip_info_t *info,
                        char *error,
                        size_t error_size) {
#define CHECK(c, s)                                                                                \
    do {                                                                                           \
        if (!(c))                                                                                  \
            return fail(error, error_size, s);                                                     \
    } while (0)
    CHECK(data && info && size >= 22, "short ZIP");
    memset(info, 0, sizeof(*info));
    size_t end = size - 22, low = size > 65557 ? size - 65557 : 0;
    for (;;) {
        if (get(data + end, 4) == 0x06054b50 && get(data + end + 20, 2) == size - end - 22)
            break;
        CHECK(end > low, "missing EOCD");
        end--;
    }
    CHECK(!get(data + end + 4, 2) && !get(data + end + 6, 2), "multidisk ZIP");
    size_t count = get(data + end + 10, 2), cdsize = get(data + end + 12, 4),
           pos = get(data + end + 16, 4);
    CHECK(count == get(data + end + 8, 2), "entry count");
    CHECK(range(pos, cdsize, end) && pos + cdsize == end, "directory bounds");
    size_t directory = pos;
    info->directory_offset = directory;
    info->count = count;
    info->end_offset = end;
    info->archive_comment = data + end + 22;
    info->archive_comment_size = size - end - 22;
    CHECK(index == SIZE_MAX || index < count, "entry index");
    for (size_t i = 0; i < count; i++) {
        CHECK(range(pos, 46, end) && get(data + pos, 4) == 0x02014b50, "central header");
        size_t nn = get(data + pos + 28, 2), en = get(data + pos + 30, 2),
               cn = get(data + pos + 32, 2), local = get(data + pos + 42, 4);
        CHECK(range(pos + 46, nn + en + cn, end), "central variable fields");
        CHECK(!get(data + pos + 34, 2), "entry disk");
        CHECK(range(local, 30, directory) && get(data + local, 4) == 0x04034b50,
              "local header bounds");
        size_t dn = get(data + local + 26, 2), de = get(data + local + 28, 2), offset = local + 30;
        CHECK(range(offset, dn + de, directory), "local variable fields");
        offset += dn + de;
        if (i == index) {
            info->central_offset = pos;
            info->local_offset = local;
            info->data_offset = offset;
            info->name = data + pos + 46;
            info->name_size = nn;
            info->extra = info->name + nn;
            info->extra_size = en;
            info->comment = info->extra + en;
            info->comment_size = cn;
            info->made_by = (uint16_t)get(data + pos + 4, 2);
            info->flags = (uint16_t)get(data + pos + 8, 2);
            info->method = (uint16_t)get(data + pos + 10, 2);
            info->time = (uint16_t)get(data + pos + 12, 2);
            info->date = (uint16_t)get(data + pos + 14, 2);
            info->crc = get(data + pos + 16, 4);
            info->packed_size = get(data + pos + 20, 4);
            info->size = get(data + pos + 24, 4);
            info->internal = (uint16_t)get(data + pos + 36, 2);
            info->external = get(data + pos + 38, 4);
        }
        pos += 46 + nn + en + cn;
    }
    CHECK(pos == end, "directory size/count mismatch");
    return true;
#undef CHECK
}

unsigned char *format_zip_extract(const unsigned char *data,
                                  size_t size,
                                  size_t index,
                                  size_t max_size,
                                  size_t *output_size,
                                  char *error,
                                  size_t error_size) {
    format_zip_info_t f;
    unsigned char *out = NULL;
    const char *why = "ZIP extraction";
    if (output_size)
        *output_size = 0;
    if (!format_zip_inspect(data, size, index, &f, error, error_size))
        return NULL;
#define REQUIRE(c, s)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            why = s;                                                                               \
            goto bad;                                                                              \
        }                                                                                          \
    } while (0)
    REQUIRE(index != SIZE_MAX, "entry index");
    REQUIRE(!(f.flags & ~(uint16_t)0x808) && (f.method == 0 || f.method == 8),
            "unsupported ZIP entry");
    REQUIRE(f.size <= max_size && f.packed_size <= UINT_MAX && f.size < UINT_MAX,
            "entry size limit");
    const unsigned char *l = data + f.local_offset, *c = data + f.central_offset;
    REQUIRE(get(l + 4, 2) == 20 && get(l + 6, 2) == f.flags && get(l + 8, 2) == f.method
                && get(l + 10, 2) == f.time && get(l + 12, 2) == f.date,
            "local metadata mismatch");
    REQUIRE(get(l + 26, 2) == f.name_size && !memcmp(l + 30, f.name, f.name_size),
            "local name mismatch");
    REQUIRE(range(f.data_offset, f.packed_size, f.directory_offset), "packed data bounds");
    if (f.flags & 8) {
        size_t at = f.data_offset + f.packed_size;
        /* A signature is optional; compare unsigned first so a CRC equal to the
         * signature is not mistaken for an extra word. */
        bool unsigned_match = range(at, 12, f.directory_offset) && !memcmp(data + at, c + 16, 12);
        bool signed_match = range(at, 16, f.directory_offset) && get(data + at, 4) == 0x08074b50
                            && !memcmp(data + at + 4, c + 16, 12);
        REQUIRE(unsigned_match || signed_match, "data descriptor mismatch");
        REQUIRE((!get(l + 14, 4) && !get(l + 18, 4) && !get(l + 22, 4))
                    || !memcmp(l + 14, c + 16, 12),
                "streamed local sizes");
    } else
        REQUIRE(!memcmp(l + 14, c + 16, 12), "local sizes/CRC mismatch");
    out = malloc((size_t)f.size + 1);
    REQUIRE(out, "allocation");
    if (f.method == 0) {
        REQUIRE(f.size == f.packed_size, "stored size mismatch");
        if (f.size)
            memcpy(out, data + f.data_offset, f.size);
    } else {
        z_stream z;
        memset(&z, 0, sizeof(z));
        REQUIRE(inflateInit2(&z, -15) == Z_OK, "inflate init");
        z.next_in = (Bytef *)(data + f.data_offset);
        z.avail_in = (uInt)f.packed_size;
        z.next_out = out;
        z.avail_out = (uInt)f.size + 1;
        int rc = inflate(&z, Z_FINISH);
        bool ok = rc == Z_STREAM_END && z.total_in == f.packed_size && z.total_out == f.size;
        inflateEnd(&z);
        REQUIRE(ok, "deflate stream/size");
    }
    REQUIRE((uint32_t)crc32(0, out, (uInt)f.size) == f.crc, "CRC mismatch");
    if (output_size)
        *output_size = f.size;
    return out;
bad:
    free(out);
    fail(error, error_size, why);
    return NULL;
#undef REQUIRE
}

unsigned char *format_bytes_splice(const unsigned char *data,
                                   size_t size,
                                   size_t offset,
                                   size_t remove_size,
                                   const void *insert,
                                   size_t insert_size,
                                   size_t *output_size) {
    if (output_size)
        *output_size = 0;
    if ((size && !data) || (insert_size && !insert) || offset > size || remove_size > size - offset
        || insert_size > SIZE_MAX - (size - remove_size))
        return NULL;
    size_t result_size = size - remove_size + insert_size;
    unsigned char *result = malloc(result_size ? result_size : 1);
    if (!result)
        return NULL;
    if (offset)
        memcpy(result, data, offset);
    if (insert_size)
        memcpy(result + offset, insert, insert_size);
    size_t tail = size - offset - remove_size;
    if (tail)
        memcpy(result + offset + insert_size, data + offset + remove_size, tail);
    if (output_size)
        *output_size = result_size;
    return result;
}
