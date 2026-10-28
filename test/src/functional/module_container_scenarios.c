/** @file module_container_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent container fixtures and mutations replacing check_module_container.py.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>

enum {
    OK,
    INVALID,
    MEMORY,
    LIMIT,
    ZIP,
    LAYOUT,
    PATH,
    DUPLICATE,
    TYPE,
    REQUIRED,
    UNDECLARED,
    JSON,
    MAIN_HASH,
    NATIVE_HASH
};

typedef struct {
    unsigned char *data;
    size_t size;
} bytes_t;

typedef struct {
    const char *exe, *vectors;
    size_t count;
    yyjson_mut_doc *base, *schema;
    bytes_t bytecode;
} configuration_t;

static bool release_malloc(void *p) {
    free(p);
    return true;
}

static bool release_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool release_mut(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static void *owned(scenario_run_t *r, void *p) {
    scenario_require(r, p != NULL, "allocation failed");
    scenario_defer(r, release_malloc, p);
    return p;
}

static bytes_t copy(scenario_run_t *r, const void *p, size_t n) {
    bytes_t b = {owned(r, malloc(n + 1)), n};
    if (n)
        memcpy(b.data, p, n);
    b.data[n] = 0;
    return b;
}

static bytes_t read_file(scenario_run_t *r, const char *directory, const char *name) {
    size_t n;
    const char *p = language_read(r, language_join(r, directory, name), &n);
    return (bytes_t){(unsigned char *)p, n};
}

static yyjson_mut_doc *parse_mut(scenario_run_t *r, bytes_t b) {
    char error[256] = {0};
    yyjson_mut_doc *d = format_json_parse((const char *)b.data, b.size, error, sizeof(error));
    scenario_require(r, d != NULL, error);
    scenario_defer(r, release_mut, d);
    return d;
}

static yyjson_mut_doc *clone(scenario_run_t *r, yyjson_mut_doc *d) {
    yyjson_mut_doc *result = yyjson_mut_doc_mut_copy(d, NULL);
    scenario_require(r, result != NULL, "JSON copy");
    scenario_defer(r, release_mut, result);
    return result;
}

static bytes_t json_bytes(scenario_run_t *r, yyjson_mut_doc *d) {
    size_t n = 0;
    char *p = yyjson_mut_write(d, YYJSON_WRITE_PRETTY_TWO_SPACES, &n);
    return (bytes_t){owned(r, p), n};
}

static void set(scenario_run_t *r,
                yyjson_mut_doc *d,
                yyjson_mut_val *obj,
                const char *key,
                yyjson_mut_val *value) {
    scenario_require(r,
                     value && yyjson_mut_obj_put(obj, yyjson_mut_str(d, key), value),
                     "JSON field");
}

static void
str(scenario_run_t *r, yyjson_mut_doc *d, yyjson_mut_val *obj, const char *key, const char *value) {
    set(r, d, obj, key, yyjson_mut_strcpy(d, value));
}

static void hash(scenario_run_t *r, configuration_t *c, yyjson_mut_doc *d, bytes_t payload) {
    char digest[65], error[256] = {0};
    scenario_require(r,
                     format_binary_main_hash(yyjson_mut_doc_get_root(d),
                                             yyjson_mut_doc_get_root(c->schema),
                                             payload.data,
                                             payload.size,
                                             digest,
                                             error,
                                             sizeof(error)),
                     error);
    str(r, d, yyjson_mut_doc_get_root(d), "mainHash", digest);
}

static void patch(scenario_run_t *r, bytes_t b, size_t off, uint64_t value, unsigned width) {
    scenario_require(r,
                     format_bytes_patch_le(b.data, b.size, off, value, width),
                     "mutation offset");
}

static format_zip_info_t info(scenario_run_t *r, bytes_t b, size_t index) {
    format_zip_info_t i;
    char error[256] = {0};
    scenario_require(r, format_zip_inspect(b.data, b.size, index, &i, error, sizeof(error)), error);
    return i;
}

static bytes_t change(scenario_run_t *r, bytes_t base, size_t off, uint64_t value, unsigned width) {
    bytes_t b = copy(r, base.data, base.size);
    patch(r, b, off, value, width);
    return b;
}

static format_zip_entry_t entry(const char *name, const void *data, size_t n, unsigned method) {
    format_zip_entry_t e;
    format_zip_entry_init(&e, name, data, n, method == 8);
    e.flags = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (*p >= 128)
            e.flags = 0x800;
    e.date = 10819;
    e.time = 8355;
    e.made_by = 0x314;
    e.external = name[strlen(name) - 1] == '/' ? (040755u << 16) | 0x10 : 0100640u << 16;
    e.comment = (const unsigned char *)"entry\0comment";
    e.comment_size = 13;
    return e;
}

static bytes_t pack(scenario_run_t *r,
                    format_zip_entry_t *e,
                    size_t count,
                    bool stream,
                    const void *extra,
                    size_t extra_size) {
    for (size_t i = 0; i < count; i++) {
        e[i].extra = extra;
        e[i].extra_size = extra_size;
    }
    char error[256] = {0};
    bytes_t b = {0};
    b.data = format_zip_build(e, count, NULL, 0, stream, &b.size, error, sizeof(error));
    scenario_require(r, b.data != NULL, error);
    owned(r, b.data);
    return b;
}

static void parts(scenario_run_t *r,
                  yyjson_mut_doc *metadata,
                  bytes_t payload,
                  format_zip_entry_t e[2],
                  unsigned method) {
    bytes_t json = json_bytes(r, metadata);
    e[0] = entry("module.json", json.data, json.size, method);
    e[1] = entry("bytecode.bin", payload.data, payload.size, method);
}

static bytes_t base_zip(scenario_run_t *r, configuration_t *c, unsigned method, bool stream) {
    format_zip_entry_t e[2];
    parts(r, c->base, c->bytecode, e, method);
    return pack(r, e, 2, stream, NULL, 0);
}

static yyjson_val *check(scenario_run_t *r,
                         configuration_t *c,
                         bytes_t b,
                         unsigned allowed,
                         const char *limit,
                         int zip_error) {
    const char *args[] = {c->exe, "validate", limit, NULL};
    child_options_t options = {.executable = c->exe,
                               .arguments = args,
                               .stdin_data = b.data,
                               .stdin_size = b.size};
    scenario_command_result_t result = scenario_command_capture(r, &options);
    c->count++;
    scenario_require(r,
                     result.exit_code >= 0 && result.exit_code < 14
                         && (allowed & (1u << (unsigned)result.exit_code)),
                     "unexpected container exit status");
    if (zip_error >= 0) {
        char needle[32];
        snprintf(needle, sizeof(needle), "zip=%d;", zip_error);
        scenario_require(r, strstr(result.err, needle) != NULL, "container ZIP diagnostic");
    }
    if (result.exit_code) {
        scenario_require(r, result.out_size == 0, "partial output on failure");
        return NULL;
    }
    yyjson_doc *d = yyjson_read(result.out, result.out_size, 0);
    scenario_require(r, d != NULL, "invalid container JSON output");
    scenario_defer(r, release_doc, d);
    return yyjson_doc_get_root(d);
}

static yyjson_val *expect(scenario_run_t *r, configuration_t *c, bytes_t b, int status) {
    return check(r, c, b, 1u << status, NULL, -1);
}

static void field(scenario_run_t *r, yyjson_val *v, const char *key, bytes_t b) {
    yyjson_val *x = yyjson_obj_get(v, key);
    scenario_require(r, yyjson_is_str(x) && yyjson_get_len(x) == 2 * b.size, "hex field size");
    const char *digits = "0123456789abcdef", *p = yyjson_get_str(x);
    for (size_t i = 0; i < b.size; i++)
        scenario_require(r,
                         p[2 * i] == digits[b.data[i] >> 4]
                             && p[2 * i + 1] == digits[b.data[i] & 15],
                         "hex field content");
}

static bytes_t without_signatures(scenario_run_t *r, bytes_t b) {
    format_zip_info_t archive = info(r, b, 0);
    bytes_t out = {owned(r, malloc(b.size)), b.size - 4 * archive.count};
    size_t local = 0, central = archive.directory_offset - 4 * archive.count;
    for (size_t i = 0; i < archive.count; i++) {
        format_zip_info_t x = info(r, b, i);
        size_t end = x.data_offset + x.packed_size, length = end - x.local_offset;
        memcpy(out.data + local, b.data + x.local_offset, length);
        memcpy(out.data + local + length, b.data + end + 4, 12);
        size_t clen = 46 + x.name_size + x.extra_size + x.comment_size;
        memcpy(out.data + central, b.data + x.central_offset, clen);
        patch(r, out, central + 42, local, 4);
        local += length + 12;
        central += clen;
    }
    memcpy(out.data + central, b.data + archive.end_offset, b.size - archive.end_offset);
    patch(r, out, central + 12, central - local, 4);
    patch(r, out, central + 16, local, 4);
    return out;
}

static void positive(scenario_run_t *r, configuration_t *c) {
    field(r, expect(r, c, read_file(r, c->vectors, "empty.gbin"), OK), "bytecode", c->bytecode);
    const char *extras[] = {"", "\x55\x54\x05\0\x01\0\0\0\0", "\xfe\xca\x03\0abc"};
    const size_t sizes[] = {0, 9, 7};
    for (unsigned method = 0; method <= 8; method += 8)
        for (unsigned streaming = 0; streaming < 2; streaming++)
            for (size_t x = 0; x < 3; x++) {
                format_zip_entry_t e[2];
                parts(r, c->base, c->bytecode, e, method);
                for (size_t i = 0; i < 2; i++) {
                    e[i].extra = (const unsigned char *)extras[x];
                    e[i].extra_size = sizes[x];
                }
                bytes_t b = {0};
                char error[256] = {0};
                b.data = format_zip_build(e,
                                          2,
                                          "archive\0comment",
                                          15,
                                          streaming,
                                          &b.size,
                                          error,
                                          sizeof(error));
                scenario_require(r, b.data != NULL, error);
                owned(r, b.data);
                yyjson_val *actual = yyjson_obj_get(expect(r, c, b, OK), "metadata");
                yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
                scenario_require(r, d != NULL, "JSON allocation");
                scenario_defer(r, release_mut, d);
                scenario_require(r,
                                 format_json_equal(yyjson_val_mut_copy(d, actual),
                                                   yyjson_mut_doc_get_root(c->base)),
                                 "metadata mismatch");
                if (streaming)
                    field(r, expect(r, c, without_signatures(r, b), OK), "bytecode", c->bytecode);
            }
    for (unsigned method = 0; method <= 8; method += 8) {
        format_zip_entry_t e[3], p[2];
        parts(r, c->base, c->bytecode, p, method);
        e[0] = entry("native/", "", 0, method);
        e[1] = p[1];
        e[2] = p[0];
        yyjson_val *native =
            yyjson_obj_get(expect(r, c, pack(r, e, 3, false, NULL, 0), OK), "native");
        scenario_require(r,
                         yyjson_is_arr(native) && yyjson_arr_size(native) == 0,
                         "unexpected native entries");
    }
    format_zip_entry_t e[2];
    parts(r, c->base, c->bytecode, e, 8);
    e[1].method = 0;
    expect(r, c, pack(r, e, 2, false, NULL, 0), OK);
    const size_t lengths[] = {0, 1, 2, 3, 1024};
    unsigned char payload[1024];
    for (size_t i = 0; i < sizeof(payload); i++)
        payload[i] = (unsigned char)i;
    for (size_t i = 0; i < 5; i++) {
        bytes_t b = {payload, lengths[i]};
        yyjson_mut_doc *d = clone(r, c->base);
        hash(r, c, d, b);
        parts(r, d, b, e, 8);
        field(r, expect(r, c, pack(r, e, 2, false, NULL, 0), OK), "bytecode", b);
    }
}

static void native_and_limits(scenario_run_t *r, configuration_t *c) {
    yyjson_mut_doc *d = clone(r, c->base);
    yyjson_mut_doc *desc = parse_mut(r, read_file(r, c->vectors, "native-description.json"));
    yyjson_mut_val *root = yyjson_mut_doc_get_root(d), *dr = yyjson_mut_doc_get_root(desc);
    const char *function_json = "[{\"functionId\":1,\"file\":0,\"instruction\":0,\"parameters\":[{"
                                "\"name\":\"a\"},{\"name\":\"b\"}]}]";
    yyjson_mut_doc *functions = parse_mut(r, copy(r, function_json, strlen(function_json)));
    set(r, d, root, "functions", yyjson_mut_val_mut_copy(d, yyjson_mut_doc_get_root(functions)));
    yyjson_mut_val *variants = yyjson_mut_arr(d);
    set(r, d, root, "native", variants);
    const char *names[] = {"native/variant-1.so", "native/variant-2.dll"},
               *payloads[] = {"foreign-one", "foreign-two"};
    for (size_t i = 0; i < 2; i++) {
        yyjson_mut_val *v = yyjson_mut_obj(d);
        char digest[65];
        format_binary_sha256(payloads[i], 11, digest);
        str(r, d, v, "library", names[i]);
        str(r, d, v, "hash", digest);
        set(r, d, v, "goatAbiVersion", yyjson_mut_uint(d, 1));
        yyjson_mut_val *target = yyjson_mut_val_mut_copy(d, yyjson_mut_obj_get(dr, "target"));
        set(r, d, v, "target", target);
        set(r,
            d,
            v,
            "specializations",
            yyjson_mut_val_mut_copy(d, yyjson_mut_obj_get(dr, "specializations")));
        if (i) {
            str(r, d, target, "os", "windows");
            str(r, d, target, "architecture", "x86");
            set(r, d, target, "bits", yyjson_mut_uint(d, 32));
            yyjson_mut_val *os = yyjson_mut_arr(d);
            yyjson_mut_arr_add_uint(d, os, 10);
            set(r, d, target, "minimumOs", os);
            yyjson_mut_val *runtime = yyjson_mut_obj(d), *version = yyjson_mut_arr(d);
            yyjson_mut_arr_add_uint(d, version, 10);
            str(r, d, runtime, "family", "ucrt");
            set(r, d, runtime, "minimumVersion", version);
            set(r, d, target, "runtime", runtime);
        }
        scenario_require(r, yyjson_mut_arr_append(variants, v), "native variant allocation");
    }
    hash(r, c, d, c->bytecode);
    format_zip_entry_t e[4];
    parts(r, d, c->bytecode, e, 8);
    e[2] = entry(names[0], payloads[0], 11, 0);
    e[3] = entry(names[1], payloads[1], 11, 8);
    bytes_t data = pack(r, e, 4, false, NULL, 0);
    yyjson_val *actual = yyjson_obj_get(expect(r, c, data, OK), "native");
    scenario_require(r, yyjson_is_arr(actual) && yyjson_arr_size(actual) == 2, "native count");
    bool seen[2] = {false, false};
    for (size_t i = 0; i < 2; i++) {
        yyjson_val *v = yyjson_arr_get(actual, i), *name = yyjson_obj_get(v, "name");
        size_t j = yyjson_equals_str(name, names[0]) ? 0 : 1;
        scenario_require(r, yyjson_equals_str(name, names[j]) && !seen[j], "native name");
        seen[j] = true;
        field(r, v, "data", (bytes_t){(unsigned char *)payloads[j], 11});
    }
    for (size_t i = 0; i < 2; i++) {
        format_zip_entry_t bad[4];
        memcpy(bad, e, sizeof(e));
        bad[i + 2].data = (const unsigned char *)(i ? "foreign-two!" : "foreign-one!");
        bad[i + 2].data_size = 12;
        expect(r, c, pack(r, bad, 4, false, NULL, 0), NATIVE_HASH);
        yyjson_mut_doc *md = clone(r, d);
        yyjson_mut_val *v =
            yyjson_mut_arr_get(yyjson_mut_obj_get(yyjson_mut_doc_get_root(md), "native"), i);
        str(r, md, v, "hash", "0000000000000000000000000000000000000000000000000000000000000000");
        memcpy(bad, e, sizeof(e));
        parts(r, md, c->bytecode, bad, 8);
        expect(r, c, pack(r, bad, 4, false, NULL, 0), NATIVE_HASH);
        memcpy(bad, e, sizeof(e));
        bad[2] = e[i ? 2 : 3];
        expect(r, c, pack(r, bad, 3, false, NULL, 0), UNDECLARED);
    }
    format_zip_entry_t bad[4];
    memcpy(bad, e, sizeof(e));
    parts(r, c->base, c->bytecode, bad, 8);
    expect(r, c, pack(r, bad, 3, false, NULL, 0), UNDECLARED);
    yyjson_mut_doc *md = clone(r, d);
    str(r,
        md,
        yyjson_mut_arr_get(yyjson_mut_obj_get(yyjson_mut_doc_get_root(md), "native"), 0),
        "library",
        "native/missing.so");
    memcpy(bad, e, sizeof(e));
    parts(r, md, c->bytecode, bad, 8);
    expect(r, c, pack(r, bad, 4, false, NULL, 0), UNDECLARED);
    size_t expanded = 0;
    for (size_t i = 0; i < 4; i++)
        expanded += e[i].data_size;
    char limit[32];
    snprintf(limit, sizeof(limit), "%zu", expanded);
    check(r, c, data, 1u << OK, limit, -1);
    snprintf(limit, sizeof(limit), "%zu", expanded - 1);
    check(r, c, data, 1u << LIMIT, limit, -1);
    check(r, c, data, 1u << LIMIT, "0", -1);
    bytes_t huge = base_zip(r, c, 0, false);
    for (size_t i = 0; i < 2; i++) {
        format_zip_info_t x = info(r, huge, i);
        patch(r, huge, x.central_offset + 24, 0x90000000, 4);
        patch(r, huge, x.local_offset + 22, 0x90000000, 4);
    }
    check(r, c, huge, 1u << LIMIT, "4294967295", -1);
    for (size_t i = 2; i < 4; i++) {
        format_zip_info_t x = info(r, data, i);
        bytes_t b = change(r, data, x.central_offset + 16, 0, 4);
        patch(r, b, x.local_offset + 14, 0, 4);
        check(r, c, b, 1u << ZIP, NULL, 9);
    }
}

static void metadata_paths_types(scenario_run_t *r, configuration_t *c) {
    format_zip_entry_t e[4];
    parts(r, c->base, c->bytecode, e, 8);
    expect(r, c, pack(r, e, 0, false, NULL, 0), REQUIRED);
    for (size_t i = 0; i < 2; i++)
        expect(r, c, pack(r, e + i, 1, false, NULL, 0), REQUIRED);
    yyjson_mut_doc *d = clone(r, c->base);
    set(r, d, yyjson_mut_doc_get_root(d), "extra", yyjson_mut_true(d));
    parts(r, d, c->bytecode, e, 8);
    expect(r, c, pack(r, e, 2, false, NULL, 0), JSON);
    d = clone(r, c->base);
    str(r,
        d,
        yyjson_mut_doc_get_root(d),
        "mainHash",
        "0000000000000000000000000000000000000000000000000000000000000000");
    parts(r, d, c->bytecode, e, 8);
    expect(r, c, pack(r, e, 2, false, NULL, 0), MAIN_HASH);
    bytes_t payload = copy(r, c->bytecode.data, c->bytecode.size);
    payload.data[payload.size++] = '!';
    parts(r, c->base, payload, e, 8);
    expect(r, c, pack(r, e, 2, false, NULL, 0), MAIN_HASH);
    parts(r, c->base, c->bytecode, e, 8);
    e[0] = entry("module.json", "{\"x\":1,\"x\":2}", 13, 8);
    expect(r, c, pack(r, e, 2, false, NULL, 0), JSON);
    const char *paths[] = {"../x",
                           "/module.json",
                           "C:/x",
                           "native\\x.so",
                           "native/../x.so",
                           "native//x.so",
                           "./module.json",
                           "native/a/b.so",
                           "native/x.so/",
                           "native/x:ads.so",
                           "native/x .so",
                           "native/con.so",
                           "native/lpt9.dll",
                           "module.json\0evil",
                           "README",
                           "Native/x.so",
                           "native/😀.so"};
    for (size_t i = 0; i < sizeof(paths) / sizeof(*paths); i++) {
        parts(r, c->base, c->bytecode, e, 8);
        e[2] = entry(paths[i], "", 0, 0);
        if (i == 13)
            e[2].name_size = 16;
        e[2].flags = 0;
        e[2].external = 0100640u << 16;
        /* Explicit binary header names avoid host ZipInfo path sanitization. */
        expect(r, c, pack(r, e, 3, false, NULL, 0), PATH);
    }
    const char *duplicates[] = {"module.json",
                                "Module.json",
                                "MODULE.JSON",
                                "bytecode.bin",
                                "ByteCode.Bin"};
    for (size_t i = 0; i < 5; i++) {
        parts(r, c->base, c->bytecode, e, 8);
        e[2] = entry(duplicates[i], "", 0, 0);
        expect(r, c, pack(r, e, 3, false, NULL, 0), DUPLICATE);
    }
    parts(r, c->base, c->bytecode, e, 8);
    e[2] = e[3] = entry("native/", "", 0, 0);
    expect(r, c, pack(r, e, 4, false, NULL, 0), DUPLICATE);
    e[2] = entry("native/", "data", 4, 0);
    expect(r, c, pack(r, e, 3, false, NULL, 0), TYPE);
    const uint32_t attrs[] = {0120777u << 16,
                              0020600u << 16,
                              0060600u << 16,
                              0010600u << 16,
                              0140600u << 16,
                              0040755u << 16,
                              8,
                              16,
                              64,
                              0x400};
    for (size_t i = 0; i < sizeof(attrs) / sizeof(*attrs); i++) {
        parts(r, c->base, c->bytecode, e, 8);
        e[1].external = attrs[i];
        expect(r, c, pack(r, e, 2, false, NULL, 0), TYPE);
    }
    parts(r, c->base, c->bytecode, e, 8);
    e[2] = entry("native/", "", 0, 0);
    e[2].external = 0100644u << 16;
    expect(r, c, pack(r, e, 3, false, NULL, 0), TYPE);
    const unsigned char extras[][18] = {{1, 0, 0, 0},
                                        {0x75, 0x70, 0, 0},
                                        {0x17, 0, 0, 0},
                                        {1, 0x99, 0, 0},
                                        {0x0d, 0, 0x0d, 0},
                                        {0x6e, 0x75, 0x0e, 0, 0, 0, 0, 0, 0xff, 0xa1}};
    const size_t sizes[] = {4, 4, 4, 4, 17, 18};
    for (size_t i = 0; i < 6; i++) {
        parts(r, c->base, c->bytecode, e, 8);
        expect(r, c, pack(r, e, 2, false, extras[i], sizes[i]), ZIP);
    }
}

static void structural(scenario_run_t *r, configuration_t *c) {
    struct mutation {
        size_t offset;
        unsigned width;
        uint64_t value;
    };

    for (unsigned stream = 0; stream < 2; stream++) {
        bytes_t base = base_zip(r, c, 0, stream);
        format_zip_info_t x = info(r, base, 1);
        const struct mutation mutations[] = {{4, 2, 10},
                                             {6, 2, 1},
                                             {8, 2, 8},
                                             {10, 2, 1},
                                             {12, 2, 1},
                                             {14, 4, 123},
                                             {18, 4, 123},
                                             {22, 4, 123},
                                             {30, 1, 'x'}};
        for (size_t i = 0; i < sizeof(mutations) / sizeof(*mutations); i++)
            expect(r,
                   c,
                   change(r,
                          base,
                          x.local_offset + mutations[i].offset,
                          mutations[i].value,
                          mutations[i].width),
                   ZIP);
        const unsigned flags[] = {1, 0x40, 0x2000, 0x10, 2};
        for (size_t i = 0; i < 5; i++)
            expect(r, c, change(r, base, x.central_offset + 8, flags[i], 2), ZIP);
        expect(r, c, change(r, base, x.central_offset + 10, 99, 2), ZIP);
        expect(r, c, change(r, base, x.central_offset + 6, 45, 2), ZIP);
        if (stream)
            for (size_t off = 4; off <= 12; off += 4)
                expect(r, c, change(r, base, x.data_offset + x.packed_size + off, 123, 4), ZIP);
    }
    format_zip_entry_t e[3];
    parts(r, c->base, c->bytecode, e, 8);
    e[2] = entry("native/", "", 0, 8);
    bytes_t directory = pack(r, e, 3, false, NULL, 0);
    format_zip_info_t x = info(r, directory, 2);
    bytes_t b = change(r, directory, x.central_offset + 16, 123, 4);
    patch(r, b, x.local_offset + 14, 123, 4);
    check(r, c, b, 1u << ZIP, NULL, 9);
    bytes_t base = base_zip(r, c, 8, false);
    x = info(r, base, 1);
    check(r, c, change(r, base, x.data_offset, 7, 1), 1u << ZIP, NULL, 10);
    const size_t sizes[] = {0, 1, c->bytecode.size - 1, c->bytecode.size + 1};
    for (size_t i = 0; i < 4; i++) {
        b = change(r, base, x.central_offset + 24, sizes[i], 4);
        patch(r, b, x.local_offset + 22, sizes[i], 4);
        check(r, c, b, 1u << ZIP, NULL, 8);
    }
    b = change(r, base, x.central_offset + 24, 0xfffffffe, 4);
    patch(r, b, x.local_offset + 22, 0xfffffffe, 4);
    check(r, c, b, 1u << LIMIT, "1048576", -1);
    b = (bytes_t){owned(r, malloc(base.size + 6)), base.size + 6};
    memcpy(b.data, "prefix", 6);
    memcpy(b.data + 6, base.data, base.size);
    expect(r, c, b, LAYOUT);
    b = (bytes_t){owned(r, malloc(base.size + 7)), base.size + 7};
    memcpy(b.data, base.data, base.size);
    memcpy(b.data + base.size, "trailer", 7);
    expect(r, c, b, LAYOUT);
    format_zip_info_t first = info(r, base, 0);
    expect(r, c, change(r, base, x.central_offset + 42, first.local_offset, 4), ZIP);
    for (int delta = 1; delta >= -1; delta -= 2) {
        uint32_t size = delta > 0 ? first.packed_size + 1 : first.packed_size - 1;
        b = change(r, base, first.central_offset + 20, size, 4);
        patch(r, b, first.local_offset + 18, size, 4);
        expect(r, c, b, LAYOUT);
    }
    b = change(r, base, x.central_offset + 8, 8, 2);
    patch(r, b, x.local_offset + 6, 8, 2);
    expect(r, c, b, LAYOUT);
    bytes_t streamed = base_zip(r, c, 8, true);
    format_zip_info_t sx = info(r, streamed, 1);
    expect(r, c, change(r, streamed, sx.data_offset + sx.packed_size, 0, 4), LAYOUT);
    const struct mutation zip64[] = {{x.end_offset + 10, 2, 65535},
                                     {x.end_offset + 4, 2, 1},
                                     {x.end_offset + 6, 2, 1},
                                     {x.central_offset + 20, 4, UINT32_MAX}};
    for (size_t i = 0; i < 4; i++)
        expect(r, c, change(r, base, zip64[i].offset, zip64[i].value, zip64[i].width), ZIP);
    /* Python ZipFile.open(..., force_zip64=True) writes ZIP64 size extras
     * in both local records while retaining ordinary central sizes. */
    parts(r, c->base, c->bytecode, e, 0);
    for (size_t i = 0; i < 2; i++) {
        e[i].comment = NULL;
        e[i].comment_size = 0;
        e[i].date = 33;
        e[i].time = 0;
        e[i].external = 0600u << 16;
#ifdef _WIN32
        e[i].made_by = 45;
#else
        e[i].made_by = 0x32d;
#endif
    }
    bytes_t forced_base = pack(r, e, 2, false, NULL, 0);
    first = info(r, forced_base, 0);
    b = (bytes_t){owned(r, malloc(forced_base.size + 40)), forced_base.size + 40};
    size_t local = 0;
    for (size_t i = 0; i < 2; i++) {
        format_zip_info_t old = info(r, forced_base, i);
        size_t header = old.data_offset - old.local_offset;
        memcpy(b.data + local, forced_base.data + old.local_offset, header);
        unsigned char extra[20] = {1, 0, 16, 0};
        for (unsigned j = 0; j < 4; j++)
            extra[4 + j] = extra[12 + j] = (unsigned char)(old.size >> (8 * j));
        memcpy(b.data + local + header, extra, 20);
        memcpy(b.data + local + header + 20, forced_base.data + old.data_offset, old.packed_size);
        patch(r, b, local + 4, 45, 2);
        patch(r, b, local + 18, UINT32_MAX, 4);
        patch(r, b, local + 22, UINT32_MAX, 4);
        patch(r, b, local + 28, 20, 2);
        local += header + 20 + old.packed_size;
    }
    memcpy(b.data + local,
           forced_base.data + first.directory_offset,
           forced_base.size - first.directory_offset);
    for (size_t i = 0; i < 2; i++) {
        format_zip_info_t old = info(r, forced_base, i);
        patch(r, b, old.central_offset + 40 + 6, 45, 2);
        patch(r, b, old.central_offset + 40 + 42, old.local_offset + 20 * i, 4);
    }
    patch(r, b, first.end_offset + 40 + 16, local, 4);
    check(r, c, b, 1u << ZIP, NULL, 4);
    for (size_t n = 0; n < base.size; n++)
        expect(r, c, (bytes_t){base.data, n}, ZIP);
}

static void damage(scenario_run_t *r, configuration_t *c) {
    const char *dir = scenario_data_directory(scenario_run_context(r));
    bytes_t base = read_file(r, dir, "python-samples/container-damage-base.bin");
    bytes_t source = read_file(r, dir, "python-samples/container-damage.json");
    yyjson_doc *d = yyjson_read((const char *)source.data, source.size, 0);
    scenario_require(r, d != NULL, "frozen damage JSON");
    scenario_defer(r, release_doc, d);
    yyjson_val *cases = yyjson_doc_get_root(d);
    scenario_require(r,
                     yyjson_is_arr(cases) && yyjson_arr_size(cases) == 100,
                     "frozen damage count");
    for (size_t i = 0; i < 100; i++) {
        bytes_t b = copy(r, base.data, base.size);
        yyjson_val *mutations = yyjson_arr_get(cases, i);
        scenario_require(r, yyjson_arr_size(mutations) == 3, "frozen mutation count");
        for (size_t j = 0; j < 3; j++) {
            yyjson_val *pair = yyjson_arr_get(mutations, j), *off = yyjson_arr_get(pair, 0),
                       *mask = yyjson_arr_get(pair, 1);
            scenario_require(r,
                             yyjson_arr_size(pair) == 2 && yyjson_is_uint(off)
                                 && yyjson_is_uint(mask) && yyjson_get_uint(off) < b.size
                                 && yyjson_get_uint(mask) <= 255,
                             "frozen mutation bounds");
            b.data[yyjson_get_uint(off)] ^= (unsigned char)yyjson_get_uint(mask);
        }
        check(r, c, b, (1u << 14) - 1, NULL, -1);
    }
}

static void body(scenario_run_t *r, void *user) {
    configuration_t *c = user;
    const char *args[] = {c->exe, NULL};
    child_options_t options = {.executable = c->exe, .arguments = args};
    scenario_command_result_t api = scenario_command_capture(r, &options);
    scenario_require(r, api.exit_code == 0, "container API checks");
    c->base = parse_mut(r, read_file(r, c->vectors, "empty.module.json"));
    c->schema = parse_mut(r, read_file(r, c->vectors, "../schema-source.json"));
    c->bytecode = read_file(r, c->vectors, "empty.bytecode.bin");
    positive(r, c);
    native_and_limits(r, c);
    metadata_paths_types(r, c);
    structural(r, c);
    damage(r, c);
    printf("Module container checks passed: %zu\n", c->count);
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
        puts("integration.module_container");
        result = 0;
    } else if (argc == 6 || argc == 7) {
        fs_error_t error = {0};
        char *exe = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        char *vectors = absolute_fs_path(argv[5], &error);
        clear_fs_error(&error);
        if (exe && vectors) {
            configuration_t c = {.exe = exe, .vectors = vectors};
            scenario_definition_t definition = {"integration.module_container",
                                                180000,
                                                body,
                                                &c,
                                                NULL};
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(&definition, 1, argc == 7 ? 2 : 1, selection, &options);
        }
        FREE(vectors);
        FREE(exe);
    } else
        fputs("usage: module_container_scenarios ID HOST BUILD DATA VECTORS [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
