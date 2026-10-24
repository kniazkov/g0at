#include "zip_archive.h"

#include "unzip.h"
#include "zip.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Only the minizip callbacks touch this memory stream; no stdio callbacks. */
typedef struct {
    const uint8_t *input;
    uint8_t *output;
    size_t size, position, allocated, limit;
    zip_status_t error;
    bool writing;
} memory_stream;

struct zip_archive {
    zip_layout_t layout;
    zip_entry_t *entries;
};

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p) {
    return le16(p) | (uint32_t)le16(p + 2) << 16;
}

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static bool span(size_t offset, size_t length, size_t size) {
    return offset <= size && length <= size - offset;
}

static zip_view_t view(const uint8_t *data, size_t size) {
    return (zip_view_t){data, size};
}

static zip_status_t validate_extra(zip_view_t extra) {
    size_t offset = 0;
    while (offset < extra.size) {
        if (!span(offset, 4, extra.size))
            return ZIP_FORMAT;
        size_t length = le16(extra.data + offset + 2);
        if (!span(offset + 4, length, extra.size))
            return ZIP_FORMAT;
        if (le16(extra.data + offset) == 1)
            return ZIP_ZIP64;
        offset += 4 + length;
    }
    return ZIP_SUCCESS;
}

static voidpf ZCALLBACK mem_open(voidpf opaque, const void *name, int mode) {
    (void)name;
    memory_stream *s = opaque;
    if (((mode & ZLIB_FILEFUNC_MODE_WRITE) != 0) != s->writing)
        return NULL;
    s->position = 0;
    return s;
}

static uLong ZCALLBACK mem_read(voidpf opaque, voidpf stream, void *buffer, uLong length) {
    (void)opaque;
    memory_stream *s = stream;
    if (s->position > s->size) {
        s->error = ZIP_FORMAT;
        return 0;
    }
    size_t n = length;
    if (n > s->size - s->position)
        n = s->size - s->position;
    if (n)
        memcpy(buffer, (s->writing ? s->output : s->input) + s->position, n);
    s->position += n;
    return (uLong)n;
}

static uLong ZCALLBACK mem_write(voidpf opaque, voidpf stream, const void *buffer, uLong length) {
    (void)opaque;
    memory_stream *s = stream;
    if (s->error)
        return 0;
    if (!s->writing || !span(s->position, length, s->limit)) {
        s->error = ZIP_CAPACITY;
        return 0;
    }
    size_t end = s->position + length;
    if (end > s->allocated) {
        size_t next = s->allocated < 4096 ? 4096 : s->allocated;
        while (next < end) {
            if (next > s->limit / 2) {
                next = s->limit;
                break;
            }
            next *= 2;
        }
        if (next > s->limit)
            next = s->limit;
        uint8_t *p = realloc(s->output, next);
        if (!p) {
            s->error = ZIP_MEMORY;
            return 0;
        }
        s->output = p;
        s->allocated = next;
    }
    if (s->position > s->size)
        memset(s->output + s->size, 0, s->position - s->size);
    if (length)
        memcpy(s->output + s->position, buffer, length);
    s->position = end;
    if (end > s->size)
        s->size = end;
    return length;
}

static ZPOS64_T ZCALLBACK mem_tell(voidpf opaque, voidpf stream) {
    (void)opaque;
    return ((memory_stream *)stream)->position;
}

static long ZCALLBACK mem_seek(voidpf opaque, voidpf stream, ZPOS64_T offset, int origin) {
    (void)opaque;
    memory_stream *s = stream;
    size_t base = origin == ZLIB_FILEFUNC_SEEK_CUR   ? s->position
                  : origin == ZLIB_FILEFUNC_SEEK_END ? s->size
                                                     : 0;
    size_t limit = s->writing ? s->limit : s->size;
    if (origin < 0 || origin > 2 || base > limit || offset > limit - base) {
        s->error = s->writing ? ZIP_CAPACITY : ZIP_FORMAT;
        return -1;
    }
    s->position = base + (size_t)offset;
    return 0;
}

static int ZCALLBACK mem_close(voidpf opaque, voidpf stream) {
    (void)opaque;
    (void)stream;
    return 0;
}

static int ZCALLBACK mem_error(voidpf opaque, voidpf stream) {
    (void)opaque;
    return ((memory_stream *)stream)->error != ZIP_SUCCESS;
}

static zlib_filefunc64_def callbacks(memory_stream *s) {
    return (zlib_filefunc64_def){mem_open,
                                 mem_read,
                                 mem_write,
                                 mem_tell,
                                 mem_seek,
                                 mem_close,
                                 mem_error,
                                 s};
}

const char *zip_status_string(zip_status_t status) {
    static const char *const messages[] = {"success",
                                           "invalid argument",
                                           "out of memory",
                                           "malformed ZIP",
                                           "ZIP64 unsupported",
                                           "multidisk ZIP unsupported",
                                           "unsupported ZIP feature",
                                           "capacity exceeded",
                                           "size mismatch",
                                           "CRC mismatch",
                                           "invalid Deflate stream",
                                           "local/central header mismatch"};
    return (unsigned)status < sizeof(messages) / sizeof(*messages) ? messages[status]
                                                                   : "unknown ZIP error";
}

void zip_archive_close(zip_archive_t *archive) {
    if (archive) {
        free(archive->entries);
        free(archive);
    }
}

void zip_archive_free(void *data) {
    free(data);
}

/* Bounds-only envelope inspection supplements minizip: do not normalize away
 * prefix/trailing bytes, truncate names, or hide original local/central fields. */
static zip_status_t layout(const uint8_t *data, size_t size, zip_layout_t *out) {
    if (size < 22)
        return ZIP_FORMAT;
    size_t end = size - 22;
    size_t first = end > UINT16_MAX ? end - UINT16_MAX : 0;
    for (;;) {
        if (le32(data + end) == 0x06054b50 && span(end + 22, le16(data + end + 20), size))
            break;
        if (end == first)
            return ZIP_FORMAT;
        end--;
    }
    const uint8_t *p = data + end;
    if ((end >= 20 && le32(p - 20) == 0x07064b50) || le16(p + 10) == UINT16_MAX
        || le32(p + 12) == UINT32_MAX || le32(p + 16) == UINT32_MAX)
        return ZIP_ZIP64;
    if (le16(p + 4) || le16(p + 6) || le16(p + 8) != le16(p + 10))
        return ZIP_MULTIDISK;
    size_t cd_size = le32(p + 12), declared = le32(p + 16);
    if (cd_size > end || declared > end - cd_size)
        return ZIP_FORMAT;
    size_t central = end - cd_size;
    *out = (zip_layout_t){.bytes = view(data, size),
                          .end_record = view(p, 22),
                          .comment = view(p + 22, le16(p + 20)),
                          .central_offset = central,
                          .central_size = cd_size,
                          .prefix_size = central - declared,
                          .trailing_size = size - end - 22 - le16(p + 20),
                          .declared_central_offset = (uint32_t)declared,
                          .count = le16(p + 10)};
    return ZIP_SUCCESS;
}

zip_status_t zip_archive_open(const void *data, size_t size, zip_archive_t **out) {
    if (out)
        *out = NULL;
    if (!out || (!data && size))
        return ZIP_INVALID;
    zip_layout_t info;
    zip_status_t status = layout(data, size, &info);
    if (status)
        return status;
    zip_archive_t *a = calloc(1, sizeof(*a));
    if (!a)
        return ZIP_MEMORY;
    a->layout = info;
    if (info.count) {
        a->entries = calloc(info.count, sizeof(*a->entries));
        if (!a->entries) {
            zip_archive_close(a);
            return ZIP_MEMORY;
        }
    }
    size_t position = info.central_offset, end = position + info.central_size;
    for (size_t i = 0; i < info.count; i++) {
        if (!span(position, 46, end)) {
            status = ZIP_FORMAT;
            break;
        }
        const uint8_t *p = (const uint8_t *)data + position;
        size_t name = le16(p + 28), extra = le16(p + 30), comment = le16(p + 32);
        if (le32(p) != 0x02014b50 || !span(position + 46, name + extra + comment, end)) {
            status = ZIP_FORMAT;
            break;
        }
        if (le32(p + 20) == UINT32_MAX || le32(p + 24) == UINT32_MAX || le32(p + 42) == UINT32_MAX
            || le16(p + 34) == UINT16_MAX) {
            status = ZIP_ZIP64;
            break;
        }
        if (le16(p + 34)) {
            status = ZIP_MULTIDISK;
            break;
        }
        status = validate_extra(view(p + 46 + name, extra));
        if (status)
            break;
        size_t local = le32(p + 42);
        if (local > size - info.prefix_size) {
            status = ZIP_FORMAT;
            break;
        }
        a->entries[i] = (zip_entry_t){.header = view(p, 46),
                                      .name = view(p + 46, name),
                                      .extra = view(p + 46 + name, extra),
                                      .comment = view(p + 46 + name + extra, comment),
                                      .central_offset = position,
                                      .local_offset = local + info.prefix_size,
                                      .declared_local_offset = (uint32_t)local,
                                      .compressed_size = le32(p + 20),
                                      .size = le32(p + 24),
                                      .crc32 = le32(p + 16),
                                      .external_attributes = le32(p + 38),
                                      .made_by = le16(p + 4),
                                      .needed = le16(p + 6),
                                      .flags = le16(p + 8),
                                      .method = le16(p + 10),
                                      .time = le16(p + 12),
                                      .date = le16(p + 14),
                                      .disk = le16(p + 34),
                                      .internal_attributes = le16(p + 36)};
        position += 46 + name + extra + comment;
    }
    if (!status && position != end)
        status = ZIP_FORMAT;
    /* Cross-check the index with the upstream reader; retain raw views above. */
    if (!status) {
        memory_stream stream = {.input = data, .size = size};
        zlib_filefunc64_def io = callbacks(&stream);
        unzFile file = unzOpen2_64("memory", &io);
        if (!file)
            status = ZIP_FORMAT;
        else {
            unz_global_info64 global;
            if (unzGetGlobalInfo64(file, &global) != UNZ_OK || global.number_entry != info.count)
                status = ZIP_FORMAT;
            for (size_t i = 0; !status && i < info.count; i++) {
                unz_file_info64 item;
                int r = i ? unzGoToNextFile(file) : unzGoToFirstFile(file);
                if (r != UNZ_OK
                    || unzGetCurrentFileInfo64(file, &item, NULL, 0, NULL, 0, NULL, 0) != UNZ_OK)
                    status = ZIP_FORMAT;
                else if (unzGetOffset64(file) != a->entries[i].central_offset - info.prefix_size
                         || item.compressed_size != a->entries[i].compressed_size
                         || item.uncompressed_size != a->entries[i].size)
                    status = ZIP_ZIP64;
            }
            unzClose(file);
        }
    }
    if (status) {
        zip_archive_close(a);
        return status;
    }
    *out = a;
    return ZIP_SUCCESS;
}

zip_status_t zip_archive_layout(const zip_archive_t *a, zip_layout_t *out) {
    if (out)
        memset(out, 0, sizeof(*out));
    if (!a || !out)
        return ZIP_INVALID;
    *out = a->layout;
    return ZIP_SUCCESS;
}

zip_status_t zip_archive_entry(const zip_archive_t *a, size_t index, zip_entry_t *out) {
    if (out)
        memset(out, 0, sizeof(*out));
    if (!a || !out || index >= a->layout.count)
        return ZIP_INVALID;
    *out = a->entries[index];
    return ZIP_SUCCESS;
}

zip_status_t zip_archive_local(const zip_archive_t *a, size_t index, zip_local_t *out) {
    if (out)
        memset(out, 0, sizeof(*out));
    if (!a || !out || index >= a->layout.count)
        return ZIP_INVALID;
    size_t off = a->entries[index].local_offset;
    if (!span(off, 30, a->layout.central_offset))
        return ZIP_FORMAT;
    const uint8_t *p = a->layout.bytes.data + off;
    size_t name = le16(p + 26), extra = le16(p + 28);
    if (le32(p) != 0x04034b50 || !span(off + 30, name + extra, a->layout.central_offset))
        return ZIP_FORMAT;
    *out = (zip_local_t){.header = view(p, 30),
                         .name = view(p + 30, name),
                         .extra = view(p + 30 + name, extra),
                         .offset = off,
                         .data_offset = off + 30 + name + extra,
                         .compressed_size = le32(p + 18),
                         .size = le32(p + 22),
                         .crc32 = le32(p + 14),
                         .needed = le16(p + 4),
                         .flags = le16(p + 6),
                         .method = le16(p + 8),
                         .time = le16(p + 10),
                         .date = le16(p + 12)};
    return ZIP_SUCCESS;
}

static zip_status_t expand(zip_view_t input, void *output, size_t expected) {
    z_stream z = {0};
    int r = inflateInit2(&z, -15);
    if (r != Z_OK)
        return r == Z_MEM_ERROR ? ZIP_MEMORY : ZIP_DEFLATE;
    size_t consumed = 0, produced = 0;
    zip_status_t status = ZIP_SUCCESS;
    uint8_t overflow;
    for (;;) {
        size_t in = input.size - consumed, out = expected - produced;
        if (in > UINT_MAX)
            in = UINT_MAX;
        if (out > UINT_MAX)
            out = UINT_MAX;
        z.next_in = (Bytef *)(input.data + consumed);
        z.avail_in = (uInt)in;
        z.next_out = out ? (uint8_t *)output + produced : &overflow;
        z.avail_out = out ? (uInt)out : 1;
        r = inflate(&z, Z_NO_FLUSH);
        size_t used = in - z.avail_in, made = (out ? out : 1) - z.avail_out;
        consumed += used;
        if (!out && made) {
            status = ZIP_SIZE;
            break;
        }
        produced += made;
        if (r == Z_STREAM_END) {
            if (produced != expected)
                status = ZIP_SIZE;
            else if (consumed != input.size)
                status = ZIP_DEFLATE;
            break;
        }
        if (r != Z_OK || (!used && !made)) {
            status = r == Z_MEM_ERROR ? ZIP_MEMORY : ZIP_DEFLATE;
            break;
        }
    }
    inflateEnd(&z);
    return status;
}

zip_status_t zip_archive_read(const zip_archive_t *a,
                              size_t index,
                              void *buffer,
                              size_t capacity,
                              size_t *written) {
    if (written)
        *written = 0;
    if (!a || !written || (!buffer && capacity) || index >= a->layout.count)
        return ZIP_INVALID;
    const zip_entry_t *e = &a->entries[index];
    if (e->size > capacity)
        return ZIP_CAPACITY;
    if ((e->flags & ~0x080eu) || (e->method != 0 && e->method != 8))
        return ZIP_UNSUPPORTED;
    zip_local_t local;
    zip_status_t status = zip_archive_local(a, index, &local);
    if (status)
        return status;
    if (e->method != local.method || e->flags != local.flags || e->needed != local.needed
        || e->time != local.time || e->date != local.date || e->name.size != local.name.size
        || memcmp(e->name.data, local.name.data, e->name.size)
        || (!(e->flags & 8)
            && (e->crc32 != local.crc32 || e->size != local.size
                || e->compressed_size != local.compressed_size)))
        return ZIP_HEADER_MISMATCH;
    if (!span(local.data_offset, e->compressed_size, a->layout.central_offset))
        return ZIP_FORMAT;
    zip_view_t input = view(a->layout.bytes.data + local.data_offset, e->compressed_size);
    if (e->method == 0) {
        if (e->compressed_size != e->size)
            return ZIP_SIZE;
        if (e->size)
            memcpy(buffer, input.data, e->size);
    } else
        status = expand(input, buffer, e->size);
    if (status)
        return status;
    if ((uint32_t)crc32_z(0, buffer, e->size) != e->crc32)
        return ZIP_CRC;
    *written = e->size;
    return ZIP_SUCCESS;
}

static bool valid_view(zip_view_t v, size_t max) {
    return v.size <= max && (v.data || !v.size);
}

static char *string_copy(zip_view_t v, bool placeholder) {
    char *s = malloc(v.size + 1);
    if (s) {
        if (v.size) {
            if (placeholder)
                memset(s, ' ', v.size);
            else
                memcpy(s, v.data, v.size);
        }
        s[v.size] = 0;
    }
    return s;
}

zip_status_t zip_archive_write(const zip_write_entry_t *entries,
                               size_t count,
                               zip_view_t comment,
                               size_t max_size,
                               void **out,
                               size_t *size) {
    if (out)
        *out = NULL;
    if (size)
        *size = 0;
    if (!out || !size || (!entries && count) || !valid_view(comment, UINT16_MAX))
        return ZIP_INVALID;
    if (count >= UINT16_MAX)
        return ZIP_ZIP64;
    for (size_t i = 0; i < count; i++) {
        const zip_write_entry_t *e = &entries[i];
        if (!valid_view(e->name, UINT16_MAX) || !e->name.size
            || memchr(e->name.data, 0, e->name.size) || !valid_view(e->data, UINT32_MAX - 1)
            || !valid_view(e->local_extra, UINT16_MAX) || !valid_view(e->central_extra, UINT16_MAX)
            || !valid_view(e->comment, UINT16_MAX))
            return ZIP_INVALID;
        if ((e->method != 0 && e->method != 8) || (e->flags & ~0x0800u))
            return ZIP_UNSUPPORTED;
        zip_status_t extra_status = validate_extra(e->local_extra);
        if (!extra_status)
            extra_status = validate_extra(e->central_extra);
        if (extra_status)
            return extra_status;
    }
    if (max_size > UINT32_MAX - 1)
        max_size = UINT32_MAX - 1;
    if (max_size < 22)
        return ZIP_CAPACITY;
    memory_stream stream = {.writing = true, .limit = max_size};
    zlib_filefunc64_def io = callbacks(&stream);
    zipFile file = zipOpen2_64("memory", APPEND_STATUS_CREATE, NULL, &io);
    if (!file) {
        free(stream.output);
        return stream.error ? stream.error : ZIP_MEMORY;
    }
    zip_status_t status = ZIP_SUCCESS;
    for (size_t i = 0; i < count && !status; i++) {
        const zip_write_entry_t *e = &entries[i];
        char *name = string_copy(e->name, false), *note = string_copy(e->comment, true);
        if (!name || !note) {
            free(name);
            free(note);
            status = ZIP_MEMORY;
            break;
        }
        zip_fileinfo info = {0};
        info.dosDate = ((uLong)e->date << 16) | e->time;
        /* Avoid upstream calendar conversion for an explicitly zero DOS date. */
        if (!info.dosDate)
            info.dosDate = 1;
        info.internal_fa = e->internal_attributes;
        info.external_fa = e->external_attributes;
        int r = zipOpenNewFileInZip4_64(file,
                                        name,
                                        &info,
                                        e->local_extra.data,
                                        (uInt)e->local_extra.size,
                                        e->central_extra.data,
                                        (uInt)e->central_extra.size,
                                        note,
                                        e->method,
                                        9,
                                        0,
                                        -15,
                                        8,
                                        Z_DEFAULT_STRATEGY,
                                        NULL,
                                        0,
                                        e->made_by,
                                        e->flags,
                                        0);
        free(name);
        free(note);
        if (r != ZIP_OK) {
            status = stream.error                                 ? stream.error
                     : r == Z_MEM_ERROR || r == ZIP_INTERNALERROR ? ZIP_MEMORY
                                                                  : ZIP_FORMAT;
            break;
        }
        size_t position = 0;
        while (position < e->data.size) {
            size_t n = e->data.size - position;
            if (n > UINT_MAX)
                n = UINT_MAX;
            r = zipWriteInFileInZip(file, e->data.data + position, (unsigned)n);
            if (r != ZIP_OK) {
                status = stream.error ? stream.error : ZIP_DEFLATE;
                break;
            }
            position += n;
        }
        r = zipCloseFileInZip(file);
        if (!status && r != ZIP_OK)
            status = stream.error ? stream.error : ZIP_DEFLATE;
    }
    char *note = string_copy(comment, true);
    if (!note && !status)
        status = ZIP_MEMORY;
    int result = zipClose(file, note);
    free(note);
    if (!status && result != ZIP_OK)
        status = stream.error ? stream.error : ZIP_FORMAT;
    /* Upstream infers UTF-8/compression flags and text attributes. Restore exact
     * caller metadata in same-sized fields; no ZIP emission or Deflate rewrite. */
    zip_archive_t *a = NULL;
    if (!status)
        status = zip_archive_open(stream.output, stream.size, &a);
    if (!status && a->layout.count != count)
        status = ZIP_FORMAT;
    for (size_t i = 0; !status && i < count; i++) {
        const zip_write_entry_t *e = &entries[i];
        const zip_entry_t *entry = &a->entries[i];
        uint8_t *central = stream.output + entry->central_offset,
                *local = stream.output + entry->local_offset;
        put16(central + 8, e->flags);
        put16(local + 6, e->flags);
        put16(central + 12, e->time);
        put16(local + 10, e->time);
        put16(central + 14, e->date);
        put16(local + 12, e->date);
        put16(central + 36, e->internal_attributes);
        if (e->comment.size)
            memcpy((void *)entry->comment.data, e->comment.data, e->comment.size);
    }
    if (!status && comment.size)
        memcpy((void *)a->layout.comment.data, comment.data, comment.size);
    zip_archive_close(a);
    if (status) {
        free(stream.output);
        return status;
    }
    *out = stream.output;
    *size = stream.size;
    return ZIP_SUCCESS;
}
