#include "module_container.h"

#include "lib/sha256.h"
#include "module_hash.h"

#include <stdlib.h>
#include <string.h>

struct goat_module_container {
    yyjson_doc *metadata;
    zip_view_t bytecode;
    goat_container_native *native;
    size_t native_count;
};

typedef enum { PART_JSON, PART_BYTECODE, PART_NATIVE, PART_DIRECTORY } part_kind;

typedef struct {
    zip_entry_t central;
    zip_local_t local;
    size_t index;
    part_kind kind;
    zip_view_t bytes;
    bool readable;
} record;

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p) {
    return le16(p) | (uint32_t)le16(p + 2) << 16;
}

static bool equals(zip_view_t v, const char *text) {
    return v.size == strlen(text) && !memcmp(v.data, text, v.size);
}

static goat_container_status fail(goat_container_error *error,
                                  goat_container_status status,
                                  size_t index,
                                  zip_status_t zip,
                                  goat_module_json_error json) {
    *error = (goat_container_error){status, index, zip, json};
    return status;
}

static goat_container_status
zip_failure(goat_container_error *error, zip_status_t zip, size_t index) {
    return fail(error,
                zip == ZIP_MEMORY ? GOAT_CONTAINER_MEMORY : GOAT_CONTAINER_ZIP,
                index,
                zip,
                0);
}

const char *goat_container_status_string(goat_container_status status) {
    static const char *const text[] = {"success",
                                       "invalid argument",
                                       "out of memory",
                                       "size limit",
                                       "invalid ZIP",
                                       "invalid ZIP layout",
                                       "unsafe path",
                                       "duplicate path",
                                       "non-regular file",
                                       "missing required part",
                                       "undeclared file",
                                       "invalid metadata",
                                       "main hash mismatch",
                                       "native hash mismatch"};
    return (unsigned)status < sizeof(text) / sizeof(*text) ? text[status]
                                                           : "unknown container error";
}

void goat_module_container_free(goat_module_container *c) {
    if (!c)
        return;
    yyjson_doc_free(c->metadata);
    free((void *)c->bytecode.data);
    for (size_t i = 0; i < c->native_count; i++)
        free((void *)c->native[i].bytes.data);
    free(c->native);
    free(c);
}

const yyjson_doc *goat_module_container_metadata(const goat_module_container *c) {
    return c ? c->metadata : NULL;
}

zip_view_t goat_module_container_bytecode(const goat_module_container *c) {
    return c ? c->bytecode : (zip_view_t){0};
}

size_t goat_module_container_native_count(const goat_module_container *c) {
    return c ? c->native_count : 0;
}

bool goat_module_container_native_at(const goat_module_container *c,
                                     size_t index,
                                     goat_container_native *out) {
    if (out)
        memset(out, 0, sizeof(*out));
    if (!c || !out || index >= c->native_count)
        return false;
    *out = c->native[index];
    return true;
}

static bool dos_device(zip_view_t name) {
    size_t stem = 0;
    while (stem < name.size && name.data[stem] != '.')
        stem++;
    if (stem == 3
        && (!memcmp(name.data, "con", 3) || !memcmp(name.data, "prn", 3)
            || !memcmp(name.data, "aux", 3) || !memcmp(name.data, "nul", 3)))
        return true;
    return stem == 4 && (!memcmp(name.data, "com", 3) || !memcmp(name.data, "lpt", 3))
           && name.data[3] >= '0' && name.data[3] <= '9';
}

static bool path_kind(zip_view_t name, part_kind *kind) {
    if (equals(name, "module.json")) {
        *kind = PART_JSON;
        return true;
    }
    if (equals(name, "bytecode.bin")) {
        *kind = PART_BYTECODE;
        return true;
    }
    if (equals(name, "native/")) {
        *kind = PART_DIRECTORY;
        return true;
    }
    if (name.size < 11 || memcmp(name.data, "native/", 7))
        return false;
    zip_view_t base = {name.data + 7, name.size - 7};
    if (!((base.data[0] >= 'a' && base.data[0] <= 'z')
          || (base.data[0] >= '0' && base.data[0] <= '9')))
        return false;
    for (size_t i = 0; i < base.size; i++) {
        uint8_t ch = base.data[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_'
              || ch == '-'))
            return false;
    }
    if (!((base.size > 4 && !memcmp(base.data + base.size - 4, ".dll", 4))
          || (base.size > 3 && !memcmp(base.data + base.size - 3, ".so", 3)))
        || dos_device(base))
        return false;
    *kind = PART_NATIVE;
    return true;
}

static bool file_type(uint32_t attrs, bool directory) {
    unsigned type = (attrs >> 16) & 0170000;
    if (type && type != (directory ? 0040000u : 0100000u))
        return false;
    /* Reject DOS volume labels and Windows device/reparse entries too. */
    if (attrs & (0x8u | 0x40u | 0x400u))
        return false;
    return directory || !(attrs & 0x10u);
}

static zip_status_t extras(zip_view_t extra, bool directory) {
    for (size_t p = 0; p < extra.size;) {
        if (extra.size - p < 4)
            return ZIP_FORMAT;
        unsigned tag = le16(extra.data + p), length = le16(extra.data + p + 2);
        p += 4;
        if (length > extra.size - p)
            return ZIP_FORMAT;
        if (tag == 1)
            return ZIP_ZIP64;
        /* Alternate paths and encryption must not acquire a second interpretation. */
        if (tag == 0x7075 || tag == 0x0017 || tag == 0x9901)
            return ZIP_UNSUPPORTED;
        /* PKWARE Unix can carry a link target; ASi Unix carries mode/device/link data. */
        if (tag == 0x000d && length != 12)
            return ZIP_UNSUPPORTED;
        if (tag == 0x756e
            && (length != 14 || !file_type((uint32_t)le16(extra.data + p + 4) << 16, directory)
                || le32(extra.data + p + 6) != 0))
            return ZIP_UNSUPPORTED;
        p += length;
    }
    return ZIP_SUCCESS;
}

static uint8_t fold(uint8_t c) {
    return c >= 'A' && c <= 'Z' ? (uint8_t)(c + 'a' - 'A') : c;
}

static int name_compare(zip_view_t a, zip_view_t b) {
    size_t length = a.size < b.size ? a.size : b.size;
    for (size_t i = 0; i < length; i++) {
        int difference = (int)fold(a.data[i]) - (int)fold(b.data[i]);
        if (difference)
            return difference;
    }
    return (a.size > b.size) - (a.size < b.size);
}

static int by_name(const void *a, const void *b) {
    return name_compare((*(record *const *)a)->central.name, (*(record *const *)b)->central.name);
}

static int by_offset(const void *a, const void *b) {
    size_t x = (*(record *const *)a)->local.offset, y = (*(record *const *)b)->local.offset;
    return (x > y) - (x < y);
}

static record *find_record(record **names, size_t count, const char *name, size_t length) {
    zip_view_t key = {(const uint8_t *)name, length};
    size_t low = 0, high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int cmp = name_compare(key, names[middle]->central.name);
        if (!cmp)
            return names[middle];
        if (cmp < 0)
            high = middle;
        else
            low = middle + 1;
    }
    return NULL;
}

static goat_container_status headers(zip_archive_t *zip, record *r, goat_container_error *error) {
    zip_entry_t *e = &r->central;
    zip_local_t *l = &r->local;
    zip_status_t status = zip_archive_local(zip, r->index, l);
    if (status)
        return zip_failure(error, status, r->index);
    if (l->size == UINT32_MAX || l->compressed_size == UINT32_MAX)
        return zip_failure(error, ZIP_ZIP64, r->index);
    if (e->needed < 10 || e->needed > 20 || ((e->method == 8 || (e->flags & 8)) && e->needed != 20)
        || (e->method != 0 && e->method != 8) || (e->flags & ~(e->method == 8 ? 0x80eu : 0x808u)))
        return zip_failure(error, ZIP_UNSUPPORTED, r->index);
    if (e->needed != l->needed || e->method != l->method || e->flags != l->flags
        || e->time != l->time || e->date != l->date || e->name.size != l->name.size
        || memcmp(e->name.data, l->name.data, e->name.size))
        return zip_failure(error, ZIP_HEADER_MISMATCH, r->index);
    if (e->flags & 8) {
        if ((l->crc32 && l->crc32 != e->crc32) || (l->size && l->size != e->size)
            || (l->compressed_size && l->compressed_size != e->compressed_size))
            return zip_failure(error, ZIP_HEADER_MISMATCH, r->index);
    } else if (e->crc32 != l->crc32 || e->size != l->size
               || e->compressed_size != l->compressed_size)
        return zip_failure(error, ZIP_HEADER_MISMATCH, r->index);
    status = extras(e->extra, r->kind == PART_DIRECTORY);
    if (!status)
        status = extras(l->extra, r->kind == PART_DIRECTORY);
    return status ? zip_failure(error, status, r->index) : GOAT_CONTAINER_OK;
}

static goat_container_status
physical(record **order, size_t count, zip_layout_t layout, goat_container_error *error) {
    qsort(order, count, sizeof(*order), by_offset);
    size_t expected = 0;
    for (size_t i = 0; i < count; i++) {
        record *r = order[i];
        size_t next = i + 1 < count ? order[i + 1]->local.offset : layout.central_offset;
        if (r->local.offset != expected || r->local.data_offset > next
            || r->central.compressed_size > next - r->local.data_offset)
            return fail(error, GOAT_CONTAINER_LAYOUT, r->index, 0, 0);
        size_t end = r->local.data_offset + r->central.compressed_size;
        size_t gap = next - end;
        if (r->central.flags & 8) {
            const uint8_t *p = layout.bytes.data + end;
            if (gap == 16 && le32(p) == 0x08074b50)
                p += 4;
            else if (gap != 12)
                return fail(error, GOAT_CONTAINER_LAYOUT, r->index, 0, 0);
            if (le32(p) != r->central.crc32 || le32(p + 4) != r->central.compressed_size
                || le32(p + 8) != r->central.size)
                return zip_failure(error, ZIP_HEADER_MISMATCH, r->index);
        } else if (gap)
            return fail(error, GOAT_CONTAINER_LAYOUT, r->index, 0, 0);
        expected = next;
    }
    return expected == layout.central_offset ? GOAT_CONTAINER_OK
                                             : fail(error, GOAT_CONTAINER_LAYOUT, SIZE_MAX, 0, 0);
}

static goat_container_status
read_part(zip_archive_t *zip, record *r, zip_view_t *out, goat_container_error *error) {
    uint8_t *data = malloc(r->central.size ? r->central.size : 1);
    if (!data)
        return fail(error, GOAT_CONTAINER_MEMORY, r->index, 0, 0);
    size_t written = 0;
    zip_status_t status = zip_archive_read(zip, r->index, data, r->central.size, &written);
    if (status) {
        free(data);
        return zip_failure(error, status, r->index);
    }
    *out = (zip_view_t){data, written};
    return GOAT_CONTAINER_OK;
}

static bool native_hash(zip_view_t bytes, const char *expected) {
    uint8_t hash[SHA256_SIZE];
    sha256(bytes.data, bytes.size, hash);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < SHA256_SIZE; i++)
        if (expected[2 * i] != hex[hash[i] >> 4] || expected[2 * i + 1] != hex[hash[i] & 15])
            return false;
    return true;
}

typedef struct {
    goat_container_report_fn callback;
    void *context;
    goat_container_error first;
    record *json;
} report_state;

static void report_check(report_state *r,
                         goat_container_check check,
                         goat_container_check_state state,
                         goat_container_check prerequisite,
                         const goat_container_error *error,
                         const record *entry,
                         const char *field,
                         size_t field_length) {
    goat_container_diagnostic d = {.check = check,
                                   .state = state,
                                   .prerequisite = prerequisite,
                                   .error = {.entry_index = SIZE_MAX},
                                   .field = field,
                                   .field_length = field_length};
    if (error)
        d.error = *error;
    if (entry) {
        d.entry = entry->central.name;
        d.error.entry_index = entry->index;
    }
    if (state == GOAT_CONTAINER_CHECK_FAILED
        && (!r->first.status || d.error.status == GOAT_CONTAINER_MEMORY))
        r->first = d.error;
    if (r->callback)
        r->callback(&d, r->context);
}

static void skipped(report_state *r,
                    goat_container_check check,
                    goat_container_check prerequisite,
                    const record *entry) {
    report_check(r, check, GOAT_CONTAINER_CHECK_SKIPPED, prerequisite, NULL, entry, NULL, 0);
}

static void checked(report_state *r,
                    goat_container_check check,
                    const goat_container_error *error,
                    const record *entry) {
    report_check(r,
                 check,
                 error && error->status ? GOAT_CONTAINER_CHECK_FAILED : GOAT_CONTAINER_CHECK_PASSED,
                 GOAT_CONTAINER_CHECK_LAYOUT,
                 error,
                 entry,
                 NULL,
                 0);
}

static void
json_report(goat_module_json_error status, const char *path, size_t length, void *context) {
    report_state *r = context;
    goat_container_error error = {
        .status = status == GOAT_MODULE_JSON_MEMORY ? GOAT_CONTAINER_MEMORY : GOAT_CONTAINER_JSON,
        .json_error = status,
        .entry_index = SIZE_MAX};
    report_check(r,
                 GOAT_CONTAINER_CHECK_JSON,
                 GOAT_CONTAINER_CHECK_FAILED,
                 GOAT_CONTAINER_CHECK_CONTENT,
                 &error,
                 r->json,
                 path,
                 length);
}

goat_container_status goat_module_container_read_report(const void *data,
                                                        size_t size,
                                                        size_t max_expanded,
                                                        goat_module_container **out,
                                                        goat_container_error *error,
                                                        goat_container_report_fn report,
                                                        void *context,
                                                        goat_container_inspect_fn inspect) {
    goat_container_error local = {.entry_index = SIZE_MAX};
    report_state rs = {.callback = report, .context = context, .first = {.entry_index = SIZE_MAX}};
    if (out)
        *out = NULL;
    zip_archive_t *zip = NULL;
    goat_module_container *c = NULL;
    record *records = NULL, **order = NULL, *json = NULL, *bytecode = NULL;
    zip_layout_t layout = {0};
    bool layout_valid = false;
    if (!out || (!data && size)) {
        fail(&local, GOAT_CONTAINER_INVALID, SIZE_MAX, 0, 0);
        goto done;
    }
    if (size >= UINT32_MAX) {
        fail(&local, GOAT_CONTAINER_LIMIT, SIZE_MAX, 0, 0);
        goto done;
    }
    zip_status_t zs = zip_archive_open(data, size, &zip);
    if (zs) {
        zip_failure(&local, zs, SIZE_MAX);
        goto done;
    }
    zip_archive_layout(zip, &layout);
    if (layout.prefix_size || layout.trailing_size) {
        fail(&local, GOAT_CONTAINER_LAYOUT, SIZE_MAX, 0, 0);
        goto done;
    }
    if (!layout.count) {
        fail(&local, GOAT_CONTAINER_REQUIRED, SIZE_MAX, 0, 0);
        goto done;
    }
    if (layout.count > SIZE_MAX / sizeof(*records) || layout.count > SIZE_MAX / sizeof(*order)) {
        fail(&local, GOAT_CONTAINER_LIMIT, SIZE_MAX, 0, 0);
        goto done;
    }
    records = calloc(layout.count, sizeof(*records));
    order = malloc(layout.count * sizeof(*order));
    c = calloc(1, sizeof(*c));
    if (!records || !order || !c) {
        fail(&local, GOAT_CONTAINER_MEMORY, SIZE_MAX, 0, 0);
        goto done;
    }
    size_t total = 0, native_count = 0;
    for (size_t i = 0; i < layout.count; i++) {
        record *r = &records[i];
        r->index = i;
        order[i] = r;
        zip_archive_entry(zip, i, &r->central);
    }
    qsort(order, layout.count, sizeof(*order), by_name);
    for (size_t i = 1; i < layout.count; i++) {
        if (!name_compare(order[i - 1]->central.name, order[i]->central.name)) {
            fail(&local, GOAT_CONTAINER_DUPLICATE, order[i]->index, 0, 0);
            goto done;
        }
    }
    for (size_t i = 0; i < layout.count; i++) {
        record *r = &records[i];
        if (!path_kind(r->central.name, &r->kind)) {
            fail(&local, GOAT_CONTAINER_PATH, i, 0, 0);
            goto done;
        }
        bool dir = r->kind == PART_DIRECTORY;
        if (!file_type(r->central.external_attributes, dir) || (dir && r->central.size)) {
            fail(&local, GOAT_CONTAINER_FILE_TYPE, i, 0, 0);
            goto done;
        }
        if (r->central.size > SIZE_MAX - total || r->central.size > max_expanded - total) {
            fail(&local, GOAT_CONTAINER_LIMIT, i, 0, 0);
            goto done;
        }
        total += r->central.size;
        if (headers(zip, r, &local))
            goto done;
        if (r->kind == PART_JSON)
            json = r;
        if (r->kind == PART_BYTECODE)
            bytecode = r;
        if (r->kind == PART_NATIVE)
            native_count++;
    }
    if (!json || !bytecode) {
        fail(&local, GOAT_CONTAINER_REQUIRED, SIZE_MAX, 0, 0);
        goto done;
    }
    if (physical(order, layout.count, layout, &local))
        goto done;
    qsort(order, layout.count, sizeof(*order), by_name);
    layout_valid = true;
    checked(&rs, GOAT_CONTAINER_CHECK_LAYOUT, NULL, NULL);
    /* Once the layout is safe, one bad CRC must not hide independent entries. */
    for (size_t i = 0; i < layout.count; i++) {
        record *r = &records[i];
        local = (goat_container_error){.entry_index = SIZE_MAX};
        r->readable = !read_part(zip, r, &r->bytes, &local);
        checked(&rs, GOAT_CONTAINER_CHECK_CONTENT, &local, r);
    }
    if (json->readable) {
        rs.json = json;
        goat_module_json_error je =
            goat_module_json_read_diagnostics((const char *)json->bytes.data,
                                              json->bytes.size,
                                              GOAT_MODULE_METADATA,
                                              &c->metadata,
                                              json_report,
                                              &rs);
        if (!je)
            checked(&rs, GOAT_CONTAINER_CHECK_JSON, NULL, json);
    } else {
        skipped(&rs, GOAT_CONTAINER_CHECK_JSON, GOAT_CONTAINER_CHECK_CONTENT, json);
    }
    if (c->metadata) {
        yyjson_val *variants = yyjson_obj_get(yyjson_doc_get_root(c->metadata), "native");
        size_t count = yyjson_arr_size(variants);
        local = (goat_container_error){.entry_index = SIZE_MAX};
        if (count != native_count)
            fail(&local, GOAT_CONTAINER_UNDECLARED, SIZE_MAX, 0, 0);
        for (size_t i = 0; i < count; i++) {
            yyjson_val *v = yyjson_obj_get(yyjson_arr_get(variants, i), "library");
            record *r = find_record(order, layout.count, yyjson_get_str(v), yyjson_get_len(v));
            if (!r || r->kind != PART_NATIVE || r->central.name.size != yyjson_get_len(v)
                || memcmp(r->central.name.data, yyjson_get_str(v), yyjson_get_len(v)))
                fail(&local, GOAT_CONTAINER_UNDECLARED, SIZE_MAX, 0, 0);
        }
        checked(&rs, GOAT_CONTAINER_CHECK_DECLARATIONS, &local, NULL);
        bool declarations_ok = !local.status;
        if (declarations_ok && count) {
            c->native = calloc(count, sizeof(*c->native));
            if (!c->native) {
                fail(&local, GOAT_CONTAINER_MEMORY, SIZE_MAX, 0, 0);
                checked(&rs, GOAT_CONTAINER_CHECK_NATIVE_HASH, &local, NULL);
            } else {
                c->native_count = count;
            }
        }
        for (size_t i = 0; i < count; i++) {
            yyjson_val *v = yyjson_arr_get(variants, i), *name = yyjson_obj_get(v, "library");
            record *r =
                find_record(order, layout.count, yyjson_get_str(name), yyjson_get_len(name));
            if (!r || r->kind != PART_NATIVE) {
                skipped(&rs,
                        GOAT_CONTAINER_CHECK_NATIVE_HASH,
                        GOAT_CONTAINER_CHECK_DECLARATIONS,
                        NULL);
                continue;
            }
            if (!r->readable) {
                skipped(&rs, GOAT_CONTAINER_CHECK_NATIVE_HASH, GOAT_CONTAINER_CHECK_CONTENT, r);
                continue;
            }
            local = (goat_container_error){.entry_index = SIZE_MAX};
            if (!native_hash(r->bytes, yyjson_get_str(yyjson_obj_get(v, "hash"))))
                fail(&local, GOAT_CONTAINER_NATIVE_HASH, r->index, 0, 0);
            checked(&rs, GOAT_CONTAINER_CHECK_NATIVE_HASH, &local, r);
            if (c->native) {
                c->native[i].name = yyjson_get_str(name);
                c->native[i].name_length = yyjson_get_len(name);
                c->native[i].bytes = r->bytes;
                r->bytes = (zip_view_t){0};
            }
        }
        if (!count) {
            if (native_count)
                skipped(&rs,
                        GOAT_CONTAINER_CHECK_NATIVE_HASH,
                        GOAT_CONTAINER_CHECK_DECLARATIONS,
                        NULL);
            else
                checked(&rs, GOAT_CONTAINER_CHECK_NATIVE_HASH, NULL, NULL);
        }
        if (bytecode->readable) {
            bool matches = false;
            goat_module_json_error je = goat_module_main_hash_verify(c->metadata,
                                                                     bytecode->bytes.data,
                                                                     bytecode->bytes.size,
                                                                     &matches);
            local = (goat_container_error){.entry_index = SIZE_MAX};
            if (je)
                fail(&local,
                     je == GOAT_MODULE_JSON_MEMORY ? GOAT_CONTAINER_MEMORY : GOAT_CONTAINER_JSON,
                     json->index,
                     0,
                     je);
            else if (!matches)
                fail(&local, GOAT_CONTAINER_MAIN_HASH, json->index, 0, 0);
            checked(&rs, GOAT_CONTAINER_CHECK_MAIN_HASH, &local, json);
            c->bytecode = bytecode->bytes;
            bytecode->bytes = (zip_view_t){0};
        } else {
            skipped(&rs, GOAT_CONTAINER_CHECK_MAIN_HASH, GOAT_CONTAINER_CHECK_CONTENT, bytecode);
        }
    } else {
        skipped(&rs, GOAT_CONTAINER_CHECK_DECLARATIONS, GOAT_CONTAINER_CHECK_JSON, json);
        skipped(&rs, GOAT_CONTAINER_CHECK_NATIVE_HASH, GOAT_CONTAINER_CHECK_JSON, json);
        skipped(&rs, GOAT_CONTAINER_CHECK_MAIN_HASH, GOAT_CONTAINER_CHECK_JSON, json);
    }
    if (inspect && c->metadata)
        inspect(c, context);
    if (!rs.first.status) {
        *out = c;
        c = NULL;
    }
done:
    if (!layout_valid) {
        const record *entry =
            records && local.entry_index < layout.count ? &records[local.entry_index] : NULL;
        checked(&rs, GOAT_CONTAINER_CHECK_LAYOUT, &local, entry);
        for (int check = GOAT_CONTAINER_CHECK_CONTENT; check <= GOAT_CONTAINER_CHECK_MAIN_HASH;
             check++)
            skipped(&rs, (goat_container_check)check, GOAT_CONTAINER_CHECK_LAYOUT, NULL);
    }
    if (records)
        for (size_t i = 0; i < layout.count; i++)
            free((void *)records[i].bytes.data);
    free(order);
    free(records);
    zip_archive_close(zip);
    goat_module_container_free(c);
    if (error)
        *error = rs.first;
    return rs.first.status;
}

goat_container_status goat_module_container_read(const void *data,
                                                 size_t size,
                                                 size_t max_expanded,
                                                 goat_module_container **out,
                                                 goat_container_error *error) {
    return goat_module_container_read_report(data,
                                             size,
                                             max_expanded,
                                             out,
                                             error,
                                             NULL,
                                             NULL,
                                             NULL);
}
