#include "lib/archive/zip_archive.h"
#include "lib/files/file_system.h"
#include "yyjson.h"

/* This subprocess host uses assertions for API calls as well as validation.
 * Keep those checks and side effects active in Release parity runs. */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

static void hex(zip_view_t v) {
    for (size_t i = 0; i < v.size; i++)
        printf("%02x", v.data[i]);
}

static void field(const char *key, zip_view_t value) {
    printf("\"%s\":\"", key);
    hex(value);
    printf("\",");
}

static zip_view_t decode(yyjson_val *obj, const char *key) {
    yyjson_val *v = yyjson_obj_get(obj, key);
    if (!v)
        return (zip_view_t){0};
    assert(yyjson_is_str(v));
    const char *s = yyjson_get_str(v);
    size_t n = yyjson_get_len(v);
    assert(!(n % 2));
    uint8_t *data = malloc(n / 2 + 1);
    assert(data);
    for (size_t i = 0; i < n; i += 2) {
        unsigned x = 0;
        assert(sscanf(s + i, "%2x", &x) == 1);
        data[i / 2] = (uint8_t)x;
    }
    return (zip_view_t){data, n / 2};
}

static unsigned number(yyjson_val *v, const char *key) {
    return (unsigned)yyjson_get_uint(yyjson_obj_get(v, key));
}

static zip_status_t write_fixture(const void *data, size_t size, size_t limit) {
    yyjson_doc *doc = yyjson_read(data, size, 0);
    assert(doc);
    yyjson_val *root = yyjson_doc_get_root(doc), *items = yyjson_obj_get(root, "entries");
    size_t count = yyjson_arr_size(items);
    zip_write_entry_t *entries = calloc(count ? count : 1, sizeof(*entries));
    assert(entries);
    for (size_t i = 0; i < count; i++) {
        yyjson_val *v = yyjson_arr_get(items, i);
        entries[i] = (zip_write_entry_t){.name = decode(v, "name"),
                                         .data = decode(v, "data"),
                                         .local_extra = decode(v, "localExtra"),
                                         .central_extra = decode(v, "centralExtra"),
                                         .comment = decode(v, "comment"),
                                         .method = (uint16_t)number(v, "method"),
                                         .flags = (uint16_t)number(v, "flags"),
                                         .time = (uint16_t)number(v, "time"),
                                         .date = (uint16_t)number(v, "date"),
                                         .made_by = (uint16_t)number(v, "madeBy"),
                                         .internal_attributes = (uint16_t)number(v, "internal"),
                                         .external_attributes = number(v, "external")};
    }
    zip_view_t comment = decode(root, "comment");
    void *output = (void *)(uintptr_t)1;
    size_t output_size = 999;
    zip_status_t status = zip_archive_write(entries, count, comment, limit, &output, &output_size);
    if (!status)
        FWRITE(output, 1, output_size, stdout);
    else
        assert(!output && !output_size);
    zip_archive_free(output);
    for (size_t i = 0; i < count; i++) {
        free((void *)entries[i].name.data);
        free((void *)entries[i].data.data);
        free((void *)entries[i].local_extra.data);
        free((void *)entries[i].central_extra.data);
        free((void *)entries[i].comment.data);
    }
    free((void *)comment.data);
    free(entries);
    yyjson_doc_free(doc);
    return status;
}

static void api_checks(void) {
    zip_archive_t *a = (void *)(uintptr_t)1;
    assert(zip_archive_open(NULL, 1, &a) == ZIP_INVALID && !a);
    assert(zip_archive_open(NULL, 0, &a) == ZIP_FORMAT && !a);
    assert(zip_archive_open(NULL, 0, NULL) == ZIP_INVALID);
    size_t written = 1;
    assert(zip_archive_read(NULL, 0, NULL, 0, &written) == ZIP_INVALID && !written);
    zip_archive_close(NULL);
    zip_archive_free(NULL);
    void *data = (void *)(uintptr_t)1;
    size_t size = 1;
    assert(zip_archive_write(NULL, 0, (zip_view_t){0}, 21, &data, &size) == ZIP_CAPACITY && !data
           && !size);
    assert(zip_archive_write(NULL, 1, (zip_view_t){0}, 100, &data, &size) == ZIP_INVALID);
    assert(zip_archive_write(NULL, 0, (zip_view_t){0}, 100, &data, &size) == ZIP_SUCCESS);
    assert(size == 22);
    assert(zip_archive_open(data, size, &a) == ZIP_SUCCESS);
    zip_entry_t e;
    assert(zip_archive_entry(a, 0, &e) == ZIP_INVALID);
    assert(zip_archive_read(a, 0, NULL, 0, &written) == ZIP_INVALID);
    zip_archive_close(a);
    zip_archive_free(data);
    const uint8_t name[] = "x", contents[] = {0, 1, 2, 255};
    zip_write_entry_t entry = {.name = {name, 1},
                               .data = {contents, sizeof(contents)},
                               .method = 8};
    assert(zip_archive_write(&entry, 1, (zip_view_t){0}, 1000, &data, &size) == ZIP_SUCCESS);
    assert(zip_archive_open(data, size, &a) == ZIP_SUCCESS);
    uint8_t canary[6];
    memset(canary, 0xab, sizeof(canary));
    assert(zip_archive_read(a, 0, canary + 1, 3, &written) == ZIP_CAPACITY && !written);
    for (size_t i = 0; i < sizeof(canary); i++)
        assert(canary[i] == 0xab);
    assert(zip_archive_read(a, 0, canary + 1, 4, &written) == ZIP_SUCCESS && written == 4);
    assert(canary[0] == 0xab && canary[5] == 0xab && !memcmp(canary + 1, contents, 4));
    assert(zip_archive_read(a, 0, NULL, 4, &written) == ZIP_INVALID);
    zip_archive_close(a);
    zip_archive_free(data);
    puts("ZIP API checks passed");
}

int main(int argc, char **argv) {
    if (argc == 1) {
        api_checks();
        return 0;
    }
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    size_t cap = 32 * 1024 * 1024;
    uint8_t *buffer = malloc(cap);
    assert(buffer);
    size_t size = FREAD(buffer, 1, cap, stdin);
    assert(size < cap && !FERROR(stdin));
    zip_status_t status;
    if (!strcmp(argv[1], "write"))
        status = write_fixture(buffer, size, argc > 2 ? (size_t)strtoull(argv[2], NULL, 10) : cap);
    else {
        zip_archive_t *a = NULL;
        status = zip_archive_open(buffer, size, &a);
        if (!status && !strcmp(argv[1], "read")) {
            assert(argc == 4);
            size_t index = (size_t)strtoull(argv[2], NULL, 10),
                   capacity = (size_t)strtoull(argv[3], NULL, 10), written = 0;
            assert(capacity <= cap);
            uint8_t *output = malloc(capacity + 2);
            assert(output);
            memset(output, 0xab, capacity + 2);
            status = zip_archive_read(a, index, output + 1, capacity, &written);
            assert(output[0] == 0xab && output[capacity + 1] == 0xab);
            if (!status)
                FWRITE(output + 1, 1, written, stdout);
            else
                assert(!written);
            free(output);
        } else if (!status) {
            zip_layout_t l;
            assert(zip_archive_layout(a, &l) == ZIP_SUCCESS);
            printf("{\"prefix\":%zu,\"trailing\":%zu,\"centralOffset\":%zu,",
                   l.prefix_size,
                   l.trailing_size,
                   l.central_offset);
            field("comment", l.comment);
            printf("\"entries\":[");
            for (size_t i = 0; i < l.count; i++) {
                zip_entry_t e;
                zip_local_t local;
                assert(zip_archive_entry(a, i, &e) == ZIP_SUCCESS);
                zip_status_t local_status = zip_archive_local(a, i, &local);
                printf("%s{", i ? "," : "");
                field("name", e.name);
                field("extra", e.extra);
                field("comment", e.comment);
                printf("\"method\":%u,\"flags\":%u,\"crc\":%u,\"size\":%u,\"compressed\":%u,",
                       e.method,
                       e.flags,
                       e.crc32,
                       e.size,
                       e.compressed_size);
                printf("\"time\":%u,\"date\":%u,\"madeBy\":%u,\"needed\":%u,\"internal\":%u,"
                       "\"external\":%u,",
                       e.time,
                       e.date,
                       e.made_by,
                       e.needed,
                       e.internal_attributes,
                       e.external_attributes);
                printf("\"centralOffset\":%zu,\"localOffset\":%zu,\"localStatus\":%u,",
                       e.central_offset,
                       e.local_offset,
                       local_status);
                field("localName", local.name);
                field("localExtra", local.extra);
                printf("\"localFlags\":%u,\"localMethod\":%u,\"dataOffset\":%zu}",
                       local.flags,
                       local.method,
                       local.data_offset);
            }
            printf("]}");
        }
        zip_archive_close(a);
    }
    free(buffer);
    if (status)
        FPRINTF(stderr, "%s\n", zip_status_string(status));
    return status;
}
