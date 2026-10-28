/** @file zip_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent ZIP reader/writer scenarios replacing check_zip.py.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <stdlib.h>
#include <string.h>

enum {
    OK,
    INVALID,
    MEMORY,
    FORMAT,
    ZIP64,
    MULTIDISK,
    UNSUPPORTED,
    CAPACITY,
    SIZE,
    CRC,
    DEFLATE,
    HEADER
};

typedef struct {
    const char *exe, *vectors;
    size_t count;
} configuration_t;

typedef struct {
    unsigned char *data;
    size_t size;
} bytes_t;

static bool release_malloc(void *p) {
    free(p);
    return true;
}

static bool release_json(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool release_mut_json(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static void *owned(scenario_run_t *run, void *p) {
    scenario_require(run, p != NULL, "allocation failed");
    scenario_defer(run, release_malloc, p);
    return p;
}

static bytes_t copy(scenario_run_t *run, const void *p, size_t n) {
    bytes_t b = {owned(run, malloc(n + 1)), n};
    if (n)
        memcpy(b.data, p, n);
    b.data[n] = 0;
    return b;
}

static void patch(scenario_run_t *run, bytes_t b, size_t offset, uint64_t value, unsigned width) {
    scenario_require(run,
                     format_bytes_patch_le(b.data, b.size, offset, value, width),
                     "invalid mutation offset");
}

static unsigned hex_digit(scenario_run_t *run, unsigned char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    scenario_require(run, false, "invalid hexadecimal output");
    return 0;
}

static bytes_t decode(scenario_run_t *run, yyjson_val *v) {
    scenario_require(run,
                     yyjson_is_str(v) && yyjson_get_len(v) % 2 == 0,
                     "missing hexadecimal field");
    size_t n = yyjson_get_len(v) / 2;
    bytes_t b = {owned(run, malloc(n + 1)), n};
    const char *s = yyjson_get_str(v);
    for (size_t i = 0; i < n; i++)
        b.data[i] = (unsigned char)((hex_digit(run, s[2 * i]) << 4) | hex_digit(run, s[2 * i + 1]));
    b.data[n] = 0;
    return b;
}

static char *encode(scenario_run_t *run, const void *p, size_t n) {
    const unsigned char *bytes = p;
    const char *digits = "0123456789abcdef";
    char *s = owned(run, malloc(2 * n + 1));
    for (size_t i = 0; i < n; i++) {
        s[2 * i] = digits[bytes[i] >> 4];
        s[2 * i + 1] = digits[bytes[i] & 15];
    }
    s[2 * n] = 0;
    return s;
}

static yyjson_val *parse(scenario_run_t *run, const void *p, size_t n) {
    yyjson_doc *d = yyjson_read(p, n, 0);
    scenario_require(run, d != NULL, "invalid JSON output");
    scenario_defer(run, release_json, d);
    return yyjson_doc_get_root(d);
}

static void number(scenario_run_t *run, yyjson_val *v, const char *key, uint64_t expected) {
    yyjson_val *field = yyjson_obj_get(v, key);
    scenario_require(run, yyjson_is_uint(field) && yyjson_get_uint(field) == expected, key);
}

static void field(scenario_run_t *run, yyjson_val *v, const char *key, const void *p, size_t n) {
    bytes_t b = decode(run, yyjson_obj_get(v, key));
    scenario_require_bytes(run, key, p, n, b.data, b.size, false);
}

static scenario_command_result_t command(scenario_run_t *run,
                                         configuration_t *c,
                                         const char *mode,
                                         bytes_t input,
                                         const char *arg1,
                                         const char *arg2,
                                         unsigned allowed) {
    const char *args[] = {c->exe, mode, arg1, arg2, NULL};
    child_options_t options = {.executable = c->exe,
                               .arguments = args,
                               .stdin_data = input.data,
                               .stdin_size = input.size};
    scenario_command_result_t result = scenario_command_capture(run, &options);
    c->count++;
    scenario_require(run,
                     result.exit_code >= 0 && result.exit_code < 12
                         && (allowed & (1u << (unsigned)result.exit_code)),
                     "unexpected ZIP host exit status");
    return result;
}

static yyjson_val *inspect(scenario_run_t *run, configuration_t *c, bytes_t data) {
    scenario_command_result_t r = command(run, c, "inspect", data, NULL, NULL, 1u << OK);
    return parse(run, r.out, r.out_size);
}

static yyjson_val *item(scenario_run_t *run, yyjson_val *meta, size_t index) {
    yyjson_val *v = yyjson_arr_get(yyjson_obj_get(meta, "entries"), index);
    scenario_require(run, yyjson_is_obj(v), "missing ZIP entry");
    return v;
}

static scenario_command_result_t read_entry(scenario_run_t *run,
                                            configuration_t *c,
                                            bytes_t data,
                                            size_t index,
                                            size_t capacity,
                                            unsigned allowed) {
    char a[32], b[32];
    snprintf(a, sizeof(a), "%zu", index);
    snprintf(b, sizeof(b), "%zu", capacity);
    return command(run, c, "read", data, a, b, allowed);
}

static void content(scenario_run_t *run,
                    configuration_t *c,
                    bytes_t data,
                    size_t index,
                    const void *p,
                    size_t n) {
    scenario_command_result_t r = read_entry(run, c, data, index, n, 1u << OK);
    scenario_require_bytes(run, "entry contents", p, n, r.out, r.out_size, false);
}

static bytes_t
build(scenario_run_t *run, const format_zip_entry_t *entries, size_t count, bool stream) {
    char error[256] = {0};
    bytes_t b = {0};
    b.data = format_zip_build(entries,
                              count,
                              "archive\0comment",
                              15,
                              stream,
                              &b.size,
                              error,
                              sizeof(error));
    scenario_require(run, b.data != NULL, error);
    owned(run, b.data);
    return b;
}

static format_zip_info_t info(scenario_run_t *run, bytes_t b, size_t index) {
    format_zip_info_t i;
    char error[256] = {0};
    scenario_require(run,
                     format_zip_inspect(b.data, b.size, index, &i, error, sizeof(error)),
                     error);
    return i;
}

static format_zip_entry_t
independent_entry(const char *name, const void *data, size_t size, bool compressed) {
    format_zip_entry_t e;
    format_zip_entry_init(&e, name, data, size, compressed);
    e.flags = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (*p >= 128)
            e.flags = 0x800;
    e.date = 10819;
    e.time = 8355;
#ifdef _WIN32
    e.made_by = 20;
#else
    e.made_by = 0x314;
#endif
    e.external = 0600u << 16;
    e.extra = (const unsigned char *)"\xfe\xca\x03\0abc";
    e.extra_size = 7;
    e.comment = (const unsigned char *)"file\0comment";
    e.comment_size = 12;
    return e;
}

static yyjson_mut_doc *new_doc(scenario_run_t *run) {
    yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
    scenario_require(run, d != NULL, "JSON allocation");
    scenario_defer(run, release_mut_json, d);
    return d;
}

static yyjson_mut_val *writer_entry(scenario_run_t *run,
                                    yyjson_mut_doc *d,
                                    const format_zip_entry_t *e,
                                    const void *local,
                                    size_t local_size) {
    yyjson_mut_val *v = yyjson_mut_obj(d);
    scenario_require(run, v != NULL, "JSON object allocation");
#define ADD_HEX(key, p, n)                                                                         \
    scenario_require(run,                                                                          \
                     yyjson_mut_obj_add_str(d, v, key, encode(run, p, n)),                         \
                     "JSON field allocation")
#define ADD_UINT(key, n)                                                                           \
    scenario_require(run, yyjson_mut_obj_add_uint(d, v, key, n), "JSON number allocation")
    ADD_HEX("name", e->name, e->name_size);
    ADD_HEX("data", e->data, e->data_size);
    ADD_HEX("comment", e->comment, e->comment_size);
    ADD_HEX("centralExtra", e->extra, e->extra_size);
    ADD_HEX("localExtra", local, local_size);
    ADD_UINT("method", e->method);
    ADD_UINT("flags", e->flags);
    ADD_UINT("date", e->date);
    ADD_UINT("time", e->time);
    ADD_UINT("madeBy", e->made_by);
    ADD_UINT("internal", e->internal);
    ADD_UINT("external", e->external);
#undef ADD_HEX
#undef ADD_UINT
    return v;
}

static bytes_t write_entries(scenario_run_t *run,
                             configuration_t *c,
                             const format_zip_entry_t *entries,
                             size_t count,
                             const void *local,
                             size_t local_size,
                             const void *comment,
                             size_t comment_size,
                             size_t limit,
                             int status) {
    yyjson_mut_doc *d = new_doc(run);
    yyjson_mut_val *root = yyjson_mut_obj(d), *array = yyjson_mut_arr(d);
    scenario_require(run, root && array, "JSON root allocation");
    yyjson_mut_doc_set_root(d, root);
    scenario_require(
        run,
        yyjson_mut_obj_add_val(d, root, "entries", array)
            && yyjson_mut_obj_add_str(d, root, "comment", encode(run, comment, comment_size)),
        "JSON writer request");
    for (size_t i = 0; i < count; i++)
        scenario_require(
            run,
            yyjson_mut_arr_append(array, writer_entry(run, d, &entries[i], local, local_size)),
            "JSON entry allocation");
    size_t size;
    char *json = yyjson_mut_write(d, 0, &size);
    owned(run, json);
    char arg[32];
    snprintf(arg, sizeof(arg), "%zu", limit);
    scenario_command_result_t result =
        command(run, c, "write", (bytes_t){(unsigned char *)json, size}, arg, NULL, 1u << status);
    return (bytes_t){(unsigned char *)result.out, result.out_size};
}

static void reader_checks(scenario_run_t *run, configuration_t *c) {
    unsigned char binary[5120], repeated[5000];
    for (size_t i = 0; i < sizeof(binary); i++)
        binary[i] = (unsigned char)i;
    for (size_t i = 0; i < sizeof(repeated); i++)
        repeated[i] = ((const unsigned char *)"abc\0\xff")[i % 5];
    format_zip_entry_t entries[] = {independent_entry("same", "", 0, false),
                                    independent_entry("same", repeated, sizeof(repeated), true),
                                    independent_entry("длинное-😀", binary, sizeof(binary), false),
                                    independent_entry("empty-deflate", "", 0, true)};
    for (unsigned stream = 0; stream < 2; stream++) {
        bytes_t b = build(run, entries, 4, stream != 0);
        yyjson_val *m = inspect(run, c, b);
        field(run, m, "comment", "archive\0comment", 15);
        scenario_require(run,
                         yyjson_arr_size(yyjson_obj_get(m, "entries")) == 4,
                         "mixed entry count");
        for (size_t i = 0; i < 4; i++) {
            yyjson_val *v = item(run, m, i);
            format_zip_entry_t *e = &entries[i];
            field(run, v, "name", e->name, e->name_size);
            number(run, v, "method", e->method);
            number(run, v, "size", e->data_size);
            field(run, v, "extra", e->extra, e->extra_size);
            field(run, v, "comment", e->comment, e->comment_size);
            scenario_require(run,
                             (yyjson_get_uint(yyjson_obj_get(v, "flags")) & 8) == (stream ? 8 : 0),
                             "descriptor flag");
            content(run, c, b, i, e->data, e->data_size);
            if (e->data_size)
                read_entry(run, c, b, i, e->data_size - 1, 1u << CAPACITY);
        }
    }
    char *long_name = owned(run, malloc(60001));
    memset(long_name, 'x', 60000);
    long_name[60000] = 0;
    format_zip_entry_t e = independent_entry(long_name, "x", 1, true);
    bytes_t b = build(run, &e, 1, false);
    field(run, item(run, inspect(run, c, b), 0), "name", long_name, 60000);
    content(run, c, b, 0, "x", 1);
    e = independent_entry("abc", "xyz", 3, false);
    bytes_t base = build(run, &e, 1, false);
    format_zip_info_t bi = info(run, base, 0);
    b = copy(run, base.data, base.size);
    patch(run, b, bi.central_offset + 47, 0, 1);
    patch(run, b, bi.local_offset + 31, 0, 1);
    field(run, item(run, inspect(run, c, b), 0), "name", "a\0c", 3);
    content(run, c, b, 0, "xyz", 3);
    e = independent_entry("x", "12345123451234512345123451234512345123451234512345", 50, true);
    bytes_t short_zip = build(run, &e, 1, false);
    for (size_t n = 0; n < short_zip.size; n++)
        command(run,
                c,
                "inspect",
                (bytes_t){short_zip.data, n},
                NULL,
                NULL,
                (1u << FORMAT) | (1u << ZIP64) | (1u << MULTIDISK));
    b = copy(run, base.data, base.size);
    patch(run, b, bi.central_offset + 46 + 3 + 2, 65535, 2);
    command(run, c, "inspect", b, NULL, NULL, 1u << FORMAT);
    b = copy(run, base.data, base.size);
    patch(run, b, bi.central_offset + 46 + 3, 1, 2);
    command(run, c, "inspect", b, NULL, NULL, 1u << ZIP64);
    e = independent_entry("x", "abcdef", 6, false);
    base = build(run, &e, 1, false);
    bi = info(run, base, 0);
    size_t local = bi.local_offset, central = bi.central_offset, data = bi.data_offset,
           end = bi.end_offset;

    struct mutation {
        size_t offset;
        unsigned width;
        uint64_t value;
    } mutations[] = {{local + 4, 2, 10},
                     {local + 10, 2, 42},
                     {local + 12, 2, 42},
                     {local + 6, 2, 1},
                     {local + 8, 2, 8},
                     {local + 14, 4, 123},
                     {local + 18, 4, 5},
                     {local + 22, 4, 5},
                     {local + 30, 1, 'z'}};

    for (size_t i = 0; i < sizeof(mutations) / sizeof(*mutations); i++) {
        b = copy(run, base.data, base.size);
        patch(run, b, mutations[i].offset, mutations[i].value, mutations[i].width);
        inspect(run, c, b);
        read_entry(run, c, b, 0, 6, 1u << HEADER);
    }
    b = copy(run, base.data, base.size);
    patch(run, b, central + 16, 0, 4);
    patch(run, b, local + 14, 0, 4);
    read_entry(run, c, b, 0, 6, 1u << CRC);
    b = copy(run, base.data, base.size);
    patch(run, b, data, 255, 1);
    read_entry(run, c, b, 0, 6, 1u << CRC);
    const unsigned flags[] = {1, 0x40, 0x2000};
    for (size_t i = 0; i < 3; i++) {
        b = copy(run, base.data, base.size);
        patch(run, b, central + 8, flags[i], 2);
        number(run, item(run, inspect(run, c, b), 0), "flags", flags[i]);
        read_entry(run, c, b, 0, 6, 1u << UNSUPPORTED);
    }
    b = copy(run, base.data, base.size);
    patch(run, b, central + 10, 99, 2);
    number(run, item(run, inspect(run, c, b), 0), "method", 99);
    read_entry(run, c, b, 0, 6, 1u << UNSUPPORTED);
    b = copy(run, base.data, base.size);
    patch(run, b, central + 4, 0x314, 2);
    patch(run, b, central + 38, 0120777u << 16, 4);
    number(run, item(run, inspect(run, c, b), 0), "external", 0120777u << 16);
    struct mutation zip64[] = {{end + 10, 2, 65535},
                               {end + 12, 4, UINT32_MAX},
                               {central + 20, 4, UINT32_MAX},
                               {central + 42, 4, UINT32_MAX},
                               {central + 34, 2, 65535}};
    for (size_t i = 0; i < sizeof(zip64) / sizeof(*zip64); i++) {
        b = copy(run, base.data, base.size);
        patch(run, b, zip64[i].offset, zip64[i].value, zip64[i].width);
        command(run, c, "inspect", b, NULL, NULL, 1u << ZIP64);
    }
    size_t disks[] = {end + 4, end + 6, central + 34};
    for (size_t i = 0; i < 3; i++) {
        b = copy(run, base.data, base.size);
        patch(run, b, disks[i], 1, 2);
        command(run, c, "inspect", b, NULL, NULL, 1u << MULTIDISK);
    }
    b = (bytes_t){owned(run, malloc(base.size + 13)), base.size + 13};
    memcpy(b.data, "prefix", 6);
    memcpy(b.data + 6, base.data, base.size);
    memcpy(b.data + 6 + base.size, "trailer", 7);
    yyjson_val *m = inspect(run, c, b);
    number(run, m, "prefix", 6);
    number(run, m, "trailing", 7);
    content(run, c, b, 0, "abcdef", 6);
    format_zip_entry_t paths[] = {independent_entry("../evil", "a", 1, false),
                                  independent_entry("/absolute", "b", 1, true),
                                  independent_entry("dir/", "", 0, false)};
    m = inspect(run, c, build(run, paths, 3, false));
    scenario_require(run, yyjson_arr_size(yyjson_obj_get(m, "entries")) == 3, "path entry count");
    for (size_t i = 0; i < 3; i++)
        field(run, item(run, m, i), "name", paths[i].name, paths[i].name_size);
    struct mutation malformed[] = {{central + 28, 2, 65535},
                                   {central + 42, 4, 0xfffffffe},
                                   {end + 12, 4, 0xfffffffe},
                                   {end + 16, 4, 0xfffffffe}};
    for (size_t i = 0; i < sizeof(malformed) / sizeof(*malformed); i++) {
        b = copy(run, base.data, base.size);
        patch(run, b, malformed[i].offset, malformed[i].value, malformed[i].width);
        command(run, c, "inspect", b, NULL, NULL, 1u << FORMAT);
    }
}

static void deflate_checks(scenario_run_t *run, configuration_t *c) {
    char payload[10000];
    memset(payload, 'a', sizeof(payload));
    format_zip_entry_t e = independent_entry("x", payload, sizeof(payload), true);
    bytes_t base = build(run, &e, 1, false);
    format_zip_info_t bi = info(run, base, 0);
    size_t central = bi.central_offset, local = bi.local_offset;
    bytes_t b = copy(run, base.data, base.size);
    patch(run, b, bi.data_offset, 7, 1);
    read_entry(run, c, b, 0, 10000, 1u << DEFLATE);
    const size_t sizes[] = {0, 1, 9999, 10001};
    for (size_t i = 0; i < 4; i++) {
        b = copy(run, base.data, base.size);
        patch(run, b, central + 24, sizes[i], 4);
        patch(run, b, local + 22, sizes[i], 4);
        read_entry(run, c, b, 0, 10001, 1u << SIZE);
    }
    const size_t packed[] = {0, 1, bi.packed_size - 1};
    for (size_t i = 0; i < 3; i++) {
        b = copy(run, base.data, base.size);
        patch(run, b, central + 20, packed[i], 4);
        patch(run, b, local + 18, packed[i], 4);
        read_entry(run, c, b, 0, 10000, 1u << DEFLATE);
    }
    b.data = format_bytes_splice(base.data, base.size, central, 0, "!", 1, &b.size);
    owned(run, b.data);
    patch(run, b, central + 1 + 20, bi.packed_size + 1, 4);
    patch(run, b, local + 18, bi.packed_size + 1, 4);
    patch(run, b, bi.end_offset + 1 + 16, central + 1, 4);
    read_entry(run, c, b, 0, 10000, 1u << DEFLATE);
}

static void writer_checks(scenario_run_t *run, configuration_t *c) {
    unsigned char binary[256], repeated[30000];
    for (size_t i = 0; i < sizeof(binary); i++)
        binary[i] = (unsigned char)i;
    for (size_t i = 0; i < sizeof(repeated); i++)
        repeated[i] = (unsigned char)"abc"[i % 3];
    const char *data_dir = scenario_data_directory(scenario_run_context(run));
    size_t frozen_size;
    const char *frozen =
        language_read(run,
                      language_join(run, data_dir, "python-samples/zip-payload.bin"),
                      &frozen_size);
    scenario_require(run, frozen_size == 65537, "frozen ZIP payload length");
    const void *payloads[] = {"", "x", "xy", "xyz", binary, repeated, frozen};
    size_t sizes[] = {0, 1, 2, 3, sizeof(binary), sizeof(repeated), frozen_size};
    const char local_extra[] = "\xfe\xca\x01\0L", central_extra[] = "\xfe\xca\x01\0C";
    for (unsigned method = 0; method <= 8; method += 8) {
        format_zip_entry_t entries[7];
        char names[7][16];
        for (size_t i = 0; i < 7; i++) {
            snprintf(names[i], sizeof(names[i]), "part-%zu", i);
            format_zip_entry_init(&entries[i], names[i], payloads[i], sizes[i], method == 8);
            entries[i].comment = (const unsigned char *)"x\0y";
            entries[i].comment_size = 3;
            entries[i].extra = (const unsigned char *)central_extra;
            entries[i].extra_size = 5;
        }
        bytes_t encoded = write_entries(run,
                                        c,
                                        entries,
                                        7,
                                        local_extra,
                                        5,
                                        "global\0comment",
                                        14,
                                        32 * 1024 * 1024,
                                        OK);
        bytes_t again = write_entries(run,
                                      c,
                                      entries,
                                      7,
                                      local_extra,
                                      5,
                                      "global\0comment",
                                      14,
                                      32 * 1024 * 1024,
                                      OK);
        scenario_require_bytes(run,
                               "deterministic writer",
                               encoded.data,
                               encoded.size,
                               again.data,
                               again.size,
                               false);
        yyjson_val *meta = inspect(run, c, encoded);
        field(run, meta, "comment", "global\0comment", 14);
        scenario_require(run,
                         yyjson_arr_size(yyjson_obj_get(meta, "entries")) == 7,
                         "writer entry count");
        for (size_t i = 0; i < 7; i++) {
            char error[256] = {0};
            size_t extracted_size;
            unsigned char *extracted = format_zip_extract(encoded.data,
                                                          encoded.size,
                                                          i,
                                                          sizes[i],
                                                          &extracted_size,
                                                          error,
                                                          sizeof(error));
            scenario_require(run, extracted != NULL, error);
            owned(run, extracted);
            scenario_require_bytes(run,
                                   "independent writer extraction",
                                   payloads[i],
                                   sizes[i],
                                   extracted,
                                   extracted_size,
                                   false);
            format_zip_info_t independent = info(run, encoded, i);
            scenario_require_bytes(run,
                                   "independent writer name",
                                   names[i],
                                   strlen(names[i]),
                                   independent.name,
                                   independent.name_size,
                                   false);
            yyjson_val *v = item(run, meta, i);
            number(run, v, "method", method);
            number(run, v, "flags", 0x800);
            number(run, v, "needed", 20);
            number(run, v, "date", 33);
            number(run, v, "time", 0);
            number(run, v, "internal", 0);
            number(run, v, "external", 0100644u << 16);
            number(run, v, "madeBy", 0x314);
            field(run, v, "comment", "x\0y", 3);
            field(run, v, "localExtra", local_extra, 5);
            field(run, v, "extra", central_extra, 5);
            content(run, c, encoded, i, payloads[i], sizes[i]);
        }
        again = write_entries(run,
                              c,
                              entries,
                              7,
                              local_extra,
                              5,
                              "global\0comment",
                              14,
                              encoded.size,
                              OK);
        scenario_require_bytes(run,
                               "exact writer capacity",
                               encoded.data,
                               encoded.size,
                               again.data,
                               again.size,
                               false);
        write_entries(run,
                      c,
                      entries,
                      7,
                      local_extra,
                      5,
                      "global\0comment",
                      14,
                      encoded.size - 1,
                      CAPACITY);
    }
    format_zip_entry_t e;
    format_zip_entry_init(&e, "name", "text\n", 5, true);
    e.time = 0x1234;
    e.date = 0;
    e.flags = 0;
    e.internal = 7;
    e.external = 123;
    e.made_by = 0x714;
    yyjson_val *m =
        item(run,
             inspect(run, c, write_entries(run, c, &e, 1, NULL, 0, NULL, 0, 32 * 1024 * 1024, OK)),
             0);
    number(run, m, "time", e.time);
    number(run, m, "date", e.date);
    number(run, m, "flags", e.flags);
    number(run, m, "internal", e.internal);
    number(run, m, "external", e.external);
    number(run, m, "madeBy", e.made_by);
    format_zip_entry_t duplicates[2];
    format_zip_entry_init(&duplicates[0], "x", "a", 1, true);
    format_zip_entry_init(&duplicates[1], "x", "b", 1, true);
    m = inspect(run,
                c,
                write_entries(run, c, duplicates, 2, NULL, 0, NULL, 0, 32 * 1024 * 1024, OK));
    scenario_require(run,
                     yyjson_arr_size(yyjson_obj_get(m, "entries")) == 2,
                     "duplicate writer names");
    m = inspect(run, c, write_entries(run, c, NULL, 0, NULL, 0, NULL, 0, 32 * 1024 * 1024, OK));
    scenario_require(run, yyjson_arr_size(yyjson_obj_get(m, "entries")) == 0, "empty archive");
    char large[100];
    memset(large, 'x', sizeof(large));
    format_zip_entry_init(&e, "x", large, sizeof(large), true);
    const size_t limits[] = {0, 21, 22, 32, 50, 100};
    for (size_t i = 0; i < 6; i++)
        write_entries(run, c, &e, 1, NULL, 0, NULL, 0, limits[i], CAPACITY);
    format_zip_entry_init(&e, "x", "", 0, true);
    e.name = (const unsigned char *)"x\0y";
    e.name_size = 3;
    write_entries(run, c, &e, 1, NULL, 0, NULL, 0, 32 * 1024 * 1024, INVALID);
    e.name_size = 0;
    write_entries(run, c, &e, 1, NULL, 0, NULL, 0, 32 * 1024 * 1024, INVALID);
    format_zip_entry_init(&e, "x", "", 0, true);
    e.method = 99;
    write_entries(run, c, &e, 1, NULL, 0, NULL, 0, 32 * 1024 * 1024, UNSUPPORTED);
    e.method = 8;
    write_entries(run, c, &e, 1, "\0", 1, NULL, 0, 32 * 1024 * 1024, FORMAT);
    e.extra = (const unsigned char *)"\x01\0\0\0";
    e.extra_size = 4;
    write_entries(run, c, &e, 1, NULL, 0, NULL, 0, 32 * 1024 * 1024, ZIP64);
    /* Independently insert a ZIP64 local extra without asking the production
     * writer to create an invalid/unsupported archive. Central sizes stay 32-bit. */
    format_zip_entry_init(&e, "x", "abc", 3, false);
    e.flags = 0;
    e.external = 0600u << 16;
#ifdef _WIN32
    e.made_by = 45;
#else
    e.made_by = 0x32d;
#endif
    bytes_t base = build(run, &e, 1, false);
    format_zip_info_t bi = info(run, base, 0);
    /* Python's force_zip64 fixture has no archive comment. */
    base.size -= 15;
    patch(run, base, bi.end_offset + 20, 0, 2);
    unsigned char extra[20] = {1, 0, 16, 0, 3, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0};
    bytes_t forced = {0};
    forced.data = format_bytes_splice(base.data,
                                      base.size,
                                      bi.data_offset,
                                      0,
                                      extra,
                                      sizeof(extra),
                                      &forced.size);
    owned(run, forced.data);
    patch(run, forced, bi.local_offset + 4, 45, 2);
    patch(run, forced, bi.local_offset + 18, UINT32_MAX, 4);
    patch(run, forced, bi.local_offset + 22, UINT32_MAX, 4);
    patch(run, forced, bi.local_offset + 28, sizeof(extra), 2);
    patch(run, forced, bi.central_offset + sizeof(extra) + 6, 45, 2);
    patch(run, forced, bi.end_offset + sizeof(extra) + 16, bi.central_offset + sizeof(extra), 4);
    bytes_t actual_extra =
        decode(run, yyjson_obj_get(item(run, inspect(run, c, forced), 0), "localExtra"));
    scenario_require(run,
                     actual_extra.size >= 2 && actual_extra.data[0] == 1
                         && actual_extra.data[1] == 0,
                     "local ZIP64 extra exposed");
    read_entry(run, c, forced, 0, 3, 1u << HEADER);
    size_t metadata_size, bytecode_size, golden_size;
    const char *metadata =
        language_read(run, language_join(run, c->vectors, "empty.module.json"), &metadata_size);
    while (metadata_size && metadata[metadata_size - 1] == '\n')
        metadata_size--;
    const char *bytecode =
        language_read(run, language_join(run, c->vectors, "empty.bytecode.bin"), &bytecode_size);
    const char *golden =
        language_read(run, language_join(run, c->vectors, "empty.gbin"), &golden_size);
    format_zip_entry_t profile[2];
    format_zip_entry_init(&profile[0], "module.json", metadata, metadata_size, true);
    format_zip_entry_init(&profile[1], "bytecode.bin", bytecode, bytecode_size, true);
    bytes_t encoded = write_entries(run, c, profile, 2, NULL, 0, NULL, 0, 32 * 1024 * 1024, OK);
    scenario_require_bytes(run,
                           "frozen profile 1",
                           golden,
                           golden_size,
                           encoded.data,
                           encoded.size,
                           false);
}

static void damage_checks(scenario_run_t *run, configuration_t *c) {
    const char *data_dir = scenario_data_directory(scenario_run_context(run));
    size_t n;
#ifdef _WIN32
    const char *name = "python-samples/zip-damage-base-windows.bin";
#else
    const char *name = "python-samples/zip-damage-base.bin";
#endif
    const char *data = language_read(run, language_join(run, data_dir, name), &n);
    bytes_t base = {(unsigned char *)data, n};
    const char *json =
        language_read(run, language_join(run, data_dir, "python-samples/zip-damage.json"), &n);
    yyjson_val *cases = parse(run, json, n);
    scenario_require(run,
                     yyjson_is_arr(cases) && yyjson_arr_size(cases) == 60,
                     "frozen damage case count");
    for (size_t i = 0; i < 60; i++) {
        bytes_t b = copy(run, base.data, base.size);
        yyjson_val *mutations = yyjson_arr_get(cases, i);
        scenario_require(run,
                         yyjson_is_arr(mutations) && yyjson_arr_size(mutations) == 3,
                         "frozen mutation count");
        for (size_t j = 0; j < 3; j++) {
            yyjson_val *pair = yyjson_arr_get(mutations, j), *offset = yyjson_arr_get(pair, 0),
                       *mask = yyjson_arr_get(pair, 1);
            scenario_require(run,
                             yyjson_arr_size(pair) == 2 && yyjson_is_uint(offset)
                                 && yyjson_is_uint(mask) && yyjson_get_uint(offset) < b.size
                                 && yyjson_get_uint(mask) <= 255,
                             "invalid frozen mutation");
            b.data[yyjson_get_uint(offset)] ^= (unsigned char)yyjson_get_uint(mask);
        }
        read_entry(run, c, b, 0, 10000, (1u << 12) - 1);
    }
}

static void body(scenario_run_t *run, void *user) {
    configuration_t *c = user;
    command(run, c, NULL, (bytes_t){0}, NULL, NULL, 1u << OK);
    reader_checks(run, c);
    deflate_checks(run, c);
    writer_checks(run, c);
    damage_checks(run, c);
    scenario_require(run, c->count == 329, "ZIP scenario command coverage count");
    printf("[ ok ] ZIP checks passed: %zu (including host API checks)\n", c->count);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t argument_error = {0};
    if (!fs_arguments(&argc, &argv, &argument_error)) {
        clear_fs_error(&argument_error);
        return 1;
    }
#endif
    int result = 1;
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        puts("integration.zip");
        result = 0;
    } else if (argc == 6 || argc == 7) {
        fs_error_t error = {0};
        char *exe = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        char *vectors = absolute_fs_path(argv[5], &error);
        clear_fs_error(&error);
        if (exe && vectors) {
            configuration_t c = {exe, vectors, 0};
            scenario_definition_t definition = {"integration.zip", 180000, body, &c, NULL};
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(&definition, 1, argc == 7 ? 2 : 1, selection, &options);
        }
        FREE(vectors);
        FREE(exe);
    } else
        fputs("usage: zip_scenarios ID HOST BUILD DATA VECTORS [--keep-work]\n", stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
