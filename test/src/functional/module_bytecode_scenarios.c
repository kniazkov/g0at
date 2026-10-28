/** @file module_bytecode_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent wire images and metadata cross-checks replacing check_module_bytecode.py.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_fixtures.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>

#define MAX UINT32_MAX
#define N(a) (sizeof(a) / sizeof((a)[0]))

enum { OK, ARGUMENT, JSON, IMAGE, FILES, FUNCTIONS, PARAMETERS, IMPORTS, EXTERNALS };

typedef struct {
    unsigned char *data;
    size_t size;
} bytes_t;

typedef struct {
    uint32_t op, flags, arg0, arg1;
} instruction_t;

typedef struct {
    const char *data;
    size_t size;
} text_t;

typedef struct {
    instruction_t ins[64];
    size_t ni;
    uint32_t files[4][4];
    size_t nf;
    uint32_t functions[4][8];
    size_t nn;
    uint32_t locations[8][6];
    size_t nl;
    bool explicit_locations;
    text_t strings[8];
    size_t ns;
    uint32_t vectors[4][8];
    size_t nv, lengths[4];
    uint32_t declarations[8][3];
    size_t nd;
} image_t;

typedef struct {
    const char *exe, *vectors;
    size_t count;
    yyjson_mut_doc *base;
    bytes_t minimal;
} configuration_t;

static bool release_doc(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool release_malloc(void *p) {
    free(p);
    return true;
}

static void *owned(scenario_run_t *r, void *p) {
    scenario_require(r, p != NULL, "allocation");
    scenario_defer(r, release_malloc, p);
    return p;
}

static bytes_t read_file(scenario_run_t *r, configuration_t *c, const char *name) {
    size_t n;
    const char *p = language_read(r, language_join(r, c->vectors, name), &n);
    return (bytes_t){(unsigned char *)p, n};
}

static yyjson_mut_doc *parse(scenario_run_t *r, const void *data, size_t size) {
    yyjson_doc *source = yyjson_read(data, size, 0);
    scenario_require(r, source != NULL, "fixture JSON parse");
    yyjson_mut_doc *d = yyjson_doc_mut_copy(source, NULL);
    yyjson_doc_free(source);
    scenario_require(r, d != NULL, "fixture JSON mutable copy");
    scenario_defer(r, release_doc, d);
    return d;
}

static yyjson_mut_doc *clone(scenario_run_t *r, yyjson_mut_doc *d) {
    yyjson_mut_doc *p = yyjson_mut_doc_mut_copy(d, NULL);
    scenario_require(r, p != NULL, "JSON copy");
    scenario_defer(r, release_doc, p);
    return p;
}

static yyjson_mut_val *root(yyjson_mut_doc *d) {
    return yyjson_mut_doc_get_root(d);
}

static yyjson_mut_val *field(yyjson_mut_doc *d, const char *key) {
    return yyjson_mut_obj_get(root(d), key);
}

static yyjson_mut_val *item(yyjson_mut_doc *d, const char *key, size_t i) {
    return yyjson_mut_arr_get(field(d, key), i);
}

static void
set(scenario_run_t *r, yyjson_mut_doc *d, yyjson_mut_val *obj, const char *key, yyjson_mut_val *v) {
    scenario_require(r, v && yyjson_mut_obj_put(obj, yyjson_mut_str(d, key), v), "JSON set");
}

static void literal(scenario_run_t *r,
                    yyjson_mut_doc *d,
                    yyjson_mut_val *obj,
                    const char *key,
                    const char *value) {
    yyjson_mut_doc *p = parse(r, value, strlen(value));
    set(r, d, obj, key, yyjson_mut_val_mut_copy(d, root(p)));
}

static yyjson_mut_doc *
changed(scenario_run_t *r, yyjson_mut_doc *d, const char *key, const char *value) {
    d = clone(r, d);
    literal(r, d, root(d), key, value);
    return d;
}

static instruction_t instruction(uint32_t op, uint32_t arg1) {
    return (instruction_t){op, 0, 0, arg1};
}

static image_t basic(const instruction_t *ins, size_t ni) {
    image_t i = {0};
    memcpy(i.ins, ins, ni * sizeof(*ins));
    i.ni = ni;
    i.nf = 1;
    i.files[0][1] = (uint32_t)ni;
    return i;
}

static void put(unsigned char *p, uint32_t v) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (unsigned char)(v >> (8 * i));
}

/* Deliberately independent little-endian writer, including UTF-32 scalar payloads. */
static bytes_t encode(scenario_run_t *r, image_t i) {
    unsigned char payload[4096] = {0};
    uint32_t descriptors[20][4] = {{0}};
    size_t np = 0, nd = 0;
    for (size_t s = 0; s < i.ns; s++) {
        np = (np + 7) & ~(size_t)7;
        size_t start = np, scalars = 0;
        for (size_t k = 0; k < i.strings[s].size;) {
            const unsigned char *t = (const unsigned char *)i.strings[s].data;
            uint32_t cp = t[k++];
            unsigned more = cp < 128 ? 0 : cp < 224 ? 1 : cp < 240 ? 2 : 3;
            if (more)
                cp &= (1u << (6 - more)) - 1;
            while (more--) {
                scenario_require(r, k < i.strings[s].size, "UTF-8 fixture");
                cp = (cp << 6) | (t[k++] & 63);
            }
            scenario_require(r, np + 4 < sizeof(payload), "fixture payload capacity");
            put(payload + np, cp);
            np += 4;
            scalars++;
        }
        np += 4;
        descriptors[nd][0] = (uint32_t)start;
        descriptors[nd][1] = (uint32_t)(np - start);
        descriptors[nd][2] = 1;
        descriptors[nd++][3] = (uint32_t)scalars;
    }
    for (size_t v = 0; v < i.nv; v++) {
        np = (np + 7) & ~(size_t)7;
        descriptors[nd][0] = (uint32_t)np;
        descriptors[nd][1] = (uint32_t)i.lengths[v] * 4;
        descriptors[nd][2] = 2;
        descriptors[nd++][3] = (uint32_t)i.lengths[v];
        for (size_t k = 0; k < i.lengths[v]; k++) {
            put(payload + np, i.vectors[v][k]);
            np += 4;
        }
    }
    for (size_t d = 0; d < i.nd; d++) {
        np = (np + 7) & ~(size_t)7;
        descriptors[nd][0] = (uint32_t)np;
        descriptors[nd][1] = 16;
        descriptors[nd][2] = 3;
        descriptors[nd++][3] = 1;
        put(payload + np, 0x4445434c);
        np += 4;
        for (size_t k = 0; k < 3; k++) {
            put(payload + np, i.declarations[d][k]);
            np += 4;
        }
    }
    if (!i.explicit_locations) {
        i.nl = i.nf;
        for (size_t f = 0; f < i.nf; f++) {
            uint32_t l[] = {i.files[f][0], i.files[f][1], (uint32_t)f, 1, 1, 0};
            memcpy(i.locations[f], l, sizeof(l));
        }
    }
    size_t od = 64 + i.ni * 8, of = od + nd * 16, on = of + i.nf * 16, ol = on + i.nn * 32,
           start = ol + i.nl * 24;
    bytes_t b = {owned(r, calloc(1, start + np)), start + np};
    memcpy(b.data, "GOATBC01", 8);
    uint32_t h[] = {1,
                    64,
                    (uint32_t)b.size,
                    (uint32_t)i.ni,
                    (uint32_t)nd,
                    (uint32_t)i.nf,
                    (uint32_t)i.nn,
                    (uint32_t)i.nl,
                    64,
                    (uint32_t)od,
                    (uint32_t)of,
                    (uint32_t)on,
                    (uint32_t)ol,
                    (uint32_t)start};
    for (size_t k = 0; k < N(h); k++)
        put(b.data + 8 + k * 4, h[k]);
    for (size_t k = 0; k < i.ni; k++) {
        unsigned char *p = b.data + 64 + k * 8;
        p[0] = (unsigned char)i.ins[k].op;
        p[1] = (unsigned char)i.ins[k].flags;
        p[2] = (unsigned char)i.ins[k].arg0;
        p[3] = (unsigned char)(i.ins[k].arg0 >> 8);
        put(p + 4, i.ins[k].arg1);
    }
    for (size_t k = 0; k < nd; k++)
        for (size_t j = 0; j < 4; j++)
            put(b.data + od + k * 16 + j * 4, descriptors[k][j]);
    for (size_t k = 0; k < i.nf; k++)
        for (size_t j = 0; j < 4; j++)
            put(b.data + of + k * 16 + j * 4, i.files[k][j]);
    for (size_t k = 0; k < i.nn; k++)
        for (size_t j = 0; j < 8; j++)
            put(b.data + on + k * 32 + j * 4, i.functions[k][j]);
    for (size_t k = 0; k < i.nl; k++)
        for (size_t j = 0; j < 6; j++)
            put(b.data + ol + k * 24 + j * 4, i.locations[k][j]);
    memcpy(b.data + start, payload, np);
    return b;
}

static void check_raw(scenario_run_t *r,
                      configuration_t *c,
                      bytes_t json,
                      bytes_t image,
                      int status,
                      unsigned entry) {
    bytes_t input = {owned(r, malloc(4 + json.size + image.size)), 4 + json.size + image.size};
    put(input.data, (uint32_t)json.size);
    memcpy(input.data + 4, json.data, json.size);
    memcpy(input.data + 4 + json.size, image.data, image.size);
    const char *args[] = {c->exe, "validate", NULL};
    child_options_t options = {.executable = c->exe,
                               .arguments = args,
                               .stdin_data = input.data,
                               .stdin_size = input.size};
    c->count++;
    scenario_command_result_t result = scenario_command_capture(r, &options);
    char label[120];
    snprintf(label,
             sizeof(label),
             "bytecode case %zu: expected status %d, got %lld",
             c->count,
             status,
             (long long)result.exit_code);
    scenario_require(r, result.exit_code == status, label);
    if (status)
        scenario_require_bytes(r, "failure stdout", "", 0, result.out, result.out_size, false);
    else {
        char expected[32];
        int n = snprintf(expected, sizeof(expected), "%u\n", entry);
        scenario_require_bytes(r, "entry", expected, (size_t)n, result.out, result.out_size, true);
    }
}

static void check(scenario_run_t *r,
                  configuration_t *c,
                  yyjson_mut_doc *d,
                  bytes_t image,
                  int status,
                  unsigned entry) {
    size_t n;
    char *p = yyjson_mut_write(d, 0, &n);
    check_raw(r, c, (bytes_t){owned(r, p), n}, image, status, entry);
}

static bytes_t external_image(scenario_run_t *r, yyjson_mut_doc *d, bytes_t image) {
    size_t n;
    char error[256] = {0};
    unsigned char *p = format_fixture_external_declarations(d,
                                                            root(d),
                                                            image.data,
                                                            image.size,
                                                            &n,
                                                            error,
                                                            sizeof(error));
    scenario_require(r, p != NULL, error);
    return (bytes_t){owned(r, p), n};
}

static void files_cases(scenario_run_t *r, configuration_t *c) {
    check(r, c, c->base, c->minimal, OK, 0);
    check(r, c, changed(r, c->base, "functions", "[]"), c->minimal, OK, 0);
    check(r,
          c,
          changed(r,
                  c->base,
                  "mainHash",
                  "\"0000000000000000000000000000000000000000000000000000000000000000\""),
          c->minimal,
          OK,
          0);
    const char *stems[] = {"empty", "two-versions"};
    for (size_t k = 0; k < N(stems); k++) {
        char name[80];
        snprintf(name, sizeof(name), "%s.module.json", stems[k]);
        bytes_t j = read_file(r, c, name);
        yyjson_mut_doc *d = parse(r, j.data, j.size);
        snprintf(name, sizeof(name), "%s.bytecode.bin", stems[k]);
        check(r, c, d, read_file(r, c, name), OK, 0);
    }
    instruction_t ins[] =
        {{0, 0, 0, 0}, {73, 0, 0, 1}, {2, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {2, 0, 0, 0}};
    image_t i = basic(ins, N(ins));
    i.nf = 2;
    uint32_t fs[][4] = {{0, 3, 1, 0}, {3, 3, 4, 0}};
    memcpy(i.files, fs, sizeof(fs));
    bytes_t multi = encode(r, i);
    yyjson_mut_doc *m = changed(r,
                                c->base,
                                "files",
                                "[{\"name\":\"not-on-disk.goat\",\"entry\":1,\"external\":true},{"
                                "\"name\":\"not-on-disk.goat\",\"entry\":4,\"external\":true}]");
    check(r, c, m, multi, OK, 1);
    check(r, c, changed(r, m, "main", "1"), multi, OK, 4);
    i.ins[1] = instruction(0, 0);
    i.ins[4] = instruction(73, 0);
    check(r, c, changed(r, m, "main", "1"), encode(r, i), OK, 4);
    const char *values[] = {"2", "4294967295", "4294967296", "-1", "0.0", "null", "\"0\""};
    for (size_t k = 0; k < N(values); k++)
        check(r, c, changed(r, m, "main", values[k]), multi, JSON, 0);
    const char *badfiles[] = {"[]", "null", "{}", "\"files\""};
    for (size_t k = 0; k < N(badfiles); k++)
        check(r, c, changed(r, m, "files", badfiles[k]), multi, JSON, 0);
    yyjson_mut_doc *d = clone(r, m);
    yyjson_mut_obj_remove_key(root(d), "files");
    check(r, c, d, multi, JSON, 0);
    const char *keys[] = {"id", "external", "entry", "entry", "entry", "entry", "entry"};
    const char *vals[] = {"0", "\"true\"", "4294967296", "-1", "1.0", "\"1\"", "null"};
    for (size_t k = 0; k < N(keys); k++) {
        d = clone(r, m);
        literal(r, d, item(d, "files", 0), keys[k], vals[k]);
        check(r, c, d, multi, JSON, 0);
    }
    const char *missing[] = {"external", "entry", "name"};
    for (size_t k = 0; k < N(missing); k++) {
        d = clone(r, m);
        yyjson_mut_obj_remove_key(item(d, "files", 0), missing[k]);
        check(r, c, d, multi, JSON, 0);
    }
    unsigned entries[] = {0, 2, 3, 6, MAX};
    for (size_t k = 0; k < N(entries); k++) {
        d = clone(r, m);
        set(r, d, item(d, "files", 0), "entry", yyjson_mut_uint(d, entries[k]));
        check(r, c, d, multi, FILES, 0);
    }
    check(r, c, c->base, multi, FILES, 0);
    check(r, c, m, c->minimal, FILES, 0);
}

static void functions_cases(scenario_run_t *r, configuration_t *c) {
    const char parameter[] = "Ж\0😀";
    instruction_t ins[] = {{1, 0, 0, 8},
                           {47, 0, 1, 1},
                           {5, 0, 0, 0},
                           {1, 0, 0, 10},
                           {47, 0, 0, 0},
                           {5, 0, 0, 0},
                           {2, 0, 0, 0},
                           {0, 0, 0, 0},
                           {7, 0, 0, 0},
                           {50, 0, 0, 0},
                           {7, 0, 0, 0},
                           {50, 0, 0, 0}};
    image_t i = basic(ins, N(ins));
    i.nn = 2;
    uint32_t fn[][8] = {{1, 0, 1, 8, 1, 1, MAX, 0}, {MAX, 0, 4, 10, 0, 0, MAX, 0}};
    memcpy(i.functions, fn, sizeof(fn));
    i.explicit_locations = true;
    i.nl = 3;
    uint32_t loc[][6] = {{0, 8, 0, 1, 1, 0}, {8, 2, 0, 2, 1, 1}, {10, 2, 0, 3, 1, MAX}};
    memcpy(i.locations, loc, sizeof(loc));
    i.ns = 1;
    i.strings[0] = (text_t){parameter, sizeof(parameter) - 1};
    i.nv = 1;
    i.lengths[0] = 1;
    bytes_t code = encode(r, i);
    yyjson_mut_doc *m = changed(
        r,
        c->base,
        "functions",
        "[{\"functionId\":1,\"file\":0,\"instruction\":1,\"parameters\":[{\"name\":\"Ж\\u0000😀\"}]"
        "},{\"functionId\":4294967295,\"file\":0,\"instruction\":4,\"parameters\":[]}]");
    check(r, c, m, code, OK, 0);
    yyjson_mut_doc *d = clone(r, m);
    yyjson_mut_val *arr = yyjson_mut_arr(d);
    yyjson_mut_arr_append(arr, yyjson_mut_val_mut_copy(d, item(m, "functions", 1)));
    yyjson_mut_arr_append(arr, yyjson_mut_val_mut_copy(d, item(m, "functions", 0)));
    set(r, d, root(d), "functions", arr);
    check(r, c, d, code, OK, 0);
    const char *names[] = {"\"Ж\\u0000\"", "\"Ж\"", "\"Ж\\u0000x\"", "\"Ж\\u0000😀x\""};
    for (size_t k = 0; k < N(names); k++) {
        d = clone(r, m);
        yyjson_mut_val *p =
            yyjson_mut_arr_get(yyjson_mut_obj_get(item(d, "functions", 0), "parameters"), 0);
        literal(r, d, p, "name", names[k]);
        check(r, c, d, code, PARAMETERS, 0);
    }
    const char *keys[] = {"functionId",
                          "instruction",
                          "instruction",
                          "parameters",
                          "functionId",
                          "functionId",
                          "functionId",
                          "functionId",
                          "functionId",
                          "functionId",
                          "file",
                          "file",
                          "instruction",
                          "instruction",
                          "parameters"};
    const char *vals[] = {"3",
                          "2",
                          "4294967295",
                          "[]",
                          "0",
                          "-1",
                          "4294967296",
                          "\"1\"",
                          "1.0",
                          "4294967295",
                          "1",
                          "4294967295",
                          "4294967296",
                          "1.0",
                          "null"};
    for (size_t k = 0; k < N(keys); k++) {
        d = clone(r, m);
        literal(r, d, item(d, "functions", 0), keys[k], vals[k]);
        check(r, c, d, code, k < 4 ? FUNCTIONS : JSON, 0);
    }
    check(r, c, c->base, code, FUNCTIONS, 0);
    check(r, c, changed(r, c->base, "functions", "[]"), code, FUNCTIONS, 0);
    check(r, c, m, c->minimal, FUNCTIONS, 0);
    d = clone(r, m);
    yyjson_mut_arr_remove(field(d, "functions"), 1);
    check(r, c, d, code, FUNCTIONS, 0);
    const char *bad[] = {"null", "{}", "\"functions\""};
    for (size_t k = 0; k < N(bad); k++)
        check(r, c, changed(r, c->base, "functions", bad[k]), c->minimal, JSON, 0);
    uint64_t ids[] = {1, MAX, 0, 2, 3, UINT64_C(4294967296)};
    for (size_t k = 0; k < N(ids); k++) {
        char ext[160];
        snprintf(ext,
                 sizeof(ext),
                 "{\"f\":{\"mutable\":false,\"type\":\"function\",\"functionId\":%llu}}",
                 (unsigned long long)ids[k]);
        d = changed(r, m, "externals", ext);
        if (k < 2) {
            bytes_t b = external_image(r, d, code);
            unsigned entry =
                (unsigned)yyjson_mut_get_uint(yyjson_mut_obj_get(item(d, "files", 0), "entry"));
            check(r, c, d, b, OK, entry);
            check(r, c, d, code, FILES, 0);
        } else
            check(r, c, d, code, JSON, 0);
    }
    check(r,
          c,
          changed(r, c->base, "externals", "{\"f\":{\"mutable\":false,\"functionId\":1}}"),
          c->minimal,
          JSON,
          0);
    d = changed(r,
                c->base,
                "externals",
                "{\"f\":{\"mutable\":false,\"functionRef\":{\"module\":\"other\",\"version\":\"1\","
                "\"functionId\":4294967295}}}");
    check(r, c, d, c->minimal, EXTERNALS, 0);
    bytes_t b = external_image(r, d, c->minimal);
    check(r,
          c,
          d,
          b,
          OK,
          (unsigned)yyjson_mut_get_uint(yyjson_mut_obj_get(item(d, "files", 0), "entry")));
    bytes_t native = read_file(r, c, "native-description.json");
    yyjson_mut_doc *source = parse(r, native.data, native.size);
    d = clone(r, m);
    yyjson_mut_val *variant = yyjson_mut_obj(d);
    const char *keep[] = {"target", "specializations"};
    for (size_t k = 0; k < N(keep); k++)
        set(r, d, variant, keep[k], yyjson_mut_val_mut_copy(d, field(source, keep[k])));
    literal(r, d, variant, "library", "\"native/test.so\"");
    literal(r,
            d,
            variant,
            "hash",
            "\"0000000000000000000000000000000000000000000000000000000000000000\"");
    literal(r, d, variant, "goatAbiVersion", "1");
    yyjson_mut_val *spec = yyjson_mut_arr_get(yyjson_mut_obj_get(variant, "specializations"), 0);
    literal(r, d, yyjson_mut_obj_get(spec, "signature"), "arguments", "[\"integer\"]");
    literal(r, d, spec, "when", "{\"argumentTypes\":[\"integer\"]}");
    arr = yyjson_mut_arr(d);
    yyjson_mut_arr_append(arr, variant);
    set(r, d, root(d), "native", arr);
    check(r, c, d, code, OK, 0);
    literal(r, d, spec, "functionId", "2");
    check(r, c, d, code, JSON, 0);
}

static bytes_t import_code(scenario_run_t *r, const text_t *names, size_t count) {
    instruction_t end = instruction(2, 0);
    image_t i = basic(&end, 1);
    i.ns = count;
    for (size_t k = 0; k < count; k++) {
        i.strings[k] = names[k];
        i.ins[i.ni++] = instruction(74, (uint32_t)k);
        i.ins[i.ni++] = instruction(5, 0);
    }
    i.files[0][1] = (uint32_t)i.ni;
    return encode(r, i);
}

#define DEP                                                                                        \
    "{\"name\":\"math\",\"version\":\"1\",\"hash\":"                                               \
    "\"0000000000000000000000000000000000000000000000000000000000000000\"}"

static void imports_cases(scenario_run_t *r, configuration_t *c) {
    yyjson_mut_doc *m = changed(r, c->base, "dependencies", "[" DEP "]");
    text_t names[] = {{"math@1", 6}, {"math@1", 6}};
    check(r, c, m, import_code(r, names, 1), OK, 0);
    check(r, c, m, import_code(r, names, 2), OK, 0);
    yyjson_mut_doc *d = changed(r, c->base, "dependencies", "[" DEP "," DEP "]");
    literal(r, d, item(d, "dependencies", 0), "version", "\"2\"");
    names[1] = (text_t){"math@2", 6};
    check(r, c, d, import_code(r, names, 2), OK, 0);
    text_t bad[] = {{"math", 4},
                    {"math@2", 6},
                    {"@1", 2},
                    {"math@", 5},
                    {"math@@1", 7},
                    {"Math@1", 6},
                    {"math@1\0tail", 11},
                    {"math@1/2", 8},
                    {"math@😀", 9},
                    {"math@1.", 7},
                    {"con@1", 5}};
    for (size_t k = 0; k < N(bad); k++)
        check(r, c, m, import_code(r, &bad[k], 1), IMPORTS, 0);
    check(r, c, c->base, import_code(r, names, 1), IMPORTS, 0);
    check(r, c, m, c->minimal, OK, 0);
    d = changed(r, c->base, "dependencies", "[" DEP "," DEP "]");
    literal(r, d, item(d, "dependencies", 1), "name", "\"unused\"");
    check(r, c, d, import_code(r, names, 1), OK, 0);
}

static image_t two(uint32_t op, uint32_t arg) {
    instruction_t ins[] = {{2, 0, 0, 0}, {op, 0, 0, arg}};
    return basic(ins, N(ins));
}

static void assignment_cases(scenario_run_t *r, configuration_t *c) {
    unsigned ops[] = {77, 79};
    for (size_t k = 0; k < N(ops); k++) {
        image_t i = two(ops[k], 0);
        i.ns = 1;
        i.strings[0] = (text_t){"x", 1};
        check(r, c, c->base, encode(r, i), OK, 0);
        i.ns = 0;
        i.nv = 1;
        check(r, c, c->base, encode(r, i), IMAGE, 0);
        i.nv = 0;
        i.ns = 1;
        i.ins[1].arg1 = 1;
        check(r, c, c->base, encode(r, i), IMAGE, 0);
    }
    unsigned counts[] = {0, 1, MAX};
    for (size_t k = 0; k < N(counts); k++)
        check(r, c, c->base, encode(r, two(78, counts[k])), OK, 0);
    image_t i = two(80, 0);
    i.ns = 1;
    i.strings[0] = (text_t){"x", 1};
    check(r, c, c->base, encode(r, i), IMAGE, 0);
    i.ins[1].arg1 = 1;
    i.nd = 1;
    check(r, c, c->base, encode(r, i), IMAGE, 0);
    i.ins[1] = instruction(69, 1);
    i.ins[2] = instruction(80, 1);
    i.ni = 3;
    i.files[0][1] = 3;
    check(r, c, c->base, encode(r, i), OK, 0);
    for (unsigned op = 77; op < 81; op++) {
        i = two(op, 0);
        i.ns = 1;
        i.strings[0] = (text_t){"x", 1};
        i.ins[1].arg0 = 1;
        check(r, c, c->base, encode(r, i), IMAGE, 0);
        i.ins[1].arg0 = 0;
        i.ins[1].flags = 1;
        check(r, c, c->base, encode(r, i), IMAGE, 0);
    }
}

static image_t declaration_image(const instruction_t *prefix,
                                 size_t np,
                                 const instruction_t *suffix,
                                 size_t ns,
                                 unsigned modifier,
                                 bool mutable,
                                 text_t name) {
    image_t i = {0};
    i.nf = 1;
    i.ns = 1;
    i.strings[0] = name;
    i.nd = 1;
    i.declarations[0][2] = modifier;
    if (np)
        memcpy(i.ins, prefix, np * sizeof(*prefix));
    i.ni = np;
    i.ins[i.ni++] = instruction(7, 0);
    i.ins[i.ni++] = instruction(mutable ? 69 : 70, 1);
    if (ns)
        memcpy(i.ins + i.ni, suffix, ns * sizeof(*suffix));
    i.ni += ns;
    i.ins[i.ni++] = instruction(2, 0);
    i.files[0][1] = (uint32_t)i.ni;
    return i;
}

static void external_cases(scenario_run_t *r, configuration_t *c) {
    const char parameter[] = "Ж\0😀";
    text_t x = {"x", 1};
    yyjson_mut_doc *m = changed(r, c->base, "externals", "{\"x\":{\"mutable\":false}}");
    check(r, c, m, encode(r, declaration_image(NULL, 0, NULL, 0, 2, false, x)), OK, 0);
    yyjson_mut_doc *named =
        changed(r, c->base, "externals", "{\"Ж\\u0000😀\":{\"mutable\":false}}");
    check(r,
          c,
          named,
          encode(r,
                 declaration_image(NULL,
                                   0,
                                   NULL,
                                   0,
                                   2,
                                   false,
                                   (text_t){parameter, sizeof(parameter) - 1})),
          OK,
          0);
    bytes_t code = encode(r, declaration_image(NULL, 0, NULL, 0, 2, false, x));
    check(r, c, m, code, OK, 0);
    check(r, c, c->base, code, EXTERNALS, 0);
    check(r,
          c,
          changed(r, c->base, "externals", "{\"y\":{\"mutable\":false}}"),
          code,
          EXTERNALS,
          0);
    yyjson_mut_doc *mutable = changed(r, c->base, "externals", "{\"x\":{\"mutable\":true}}");
    check(r, c, mutable, code, EXTERNALS, 0);
    bytes_t var = encode(r, declaration_image(NULL, 0, NULL, 0, 2, true, x));
    check(r, c, m, var, EXTERNALS, 0);
    check(r, c, mutable, var, OK, 0);
    unsigned modifiers[] = {0, 1, 3, 4};
    for (size_t k = 0; k < N(modifiers); k++) {
        code = encode(r, declaration_image(NULL, 0, NULL, 0, modifiers[k], false, x));
        check(r, c, m, code, EXTERNALS, 0);
        check(r, c, c->base, code, OK, 0);
    }
    instruction_t prefixes[][2] = {{{7, 0, 0, 0}, {55, 0, 0, 0}},
                                   {{7, 0, 0, 0}, {50, 0, 0, 0}},
                                   {{3, 0, 0, 3}, {0, 0, 0, 0}},
                                   {{3, 0, 0, 0}, {0, 0, 0, 0}}};
    for (size_t k = 0; k < N(prefixes); k++)
        check(r,
              c,
              m,
              encode(r, declaration_image(prefixes[k], k < 2 ? 2 : 1, NULL, 0, 2, false, x)),
              OK,
              0);
    instruction_t block = instruction(51, 0), end = instruction(2, 0),
                  suffix[] = {{52, 0, 0, 0}, {5, 0, 0, 0}};
    code = encode(r, declaration_image(&block, 1, suffix, N(suffix), 2, false, x));
    check(r, c, c->base, code, OK, 0);
    check(r, c, m, code, EXTERNALS, 0);
    code = encode(r, declaration_image(&end, 1, NULL, 0, 2, false, x));
    check(r, c, c->base, code, OK, 0);
    check(r, c, m, code, EXTERNALS, 0);
    instruction_t duplicate[] = {{7, 0, 0, 0},
                                 {70, 0, 0, 1},
                                 {7, 0, 0, 0},
                                 {70, 0, 0, 2},
                                 {2, 0, 0, 0}};
    image_t i = basic(duplicate, N(duplicate));
    i.ns = 1;
    i.strings[0] = x;
    i.nd = 2;
    i.declarations[0][2] = i.declarations[1][2] = 2;
    check(r, c, m, encode(r, i), EXTERNALS, 0);
    instruction_t function[] = {{1, 0, 0, 4},
                                {47, 0, 0, 0},
                                {5, 0, 0, 0},
                                {2, 0, 0, 0},
                                {7, 0, 0, 0},
                                {70, 0, 0, 1},
                                {7, 0, 0, 0},
                                {50, 0, 0, 0}};
    i = basic(function, N(function));
    i.ns = 1;
    i.strings[0] = x;
    i.nd = 1;
    i.declarations[0][2] = 2;
    i.nn = 1;
    uint32_t fn[] = {1, 0, 1, 4, 0, 0, MAX, 0};
    memcpy(i.functions[0], fn, sizeof(fn));
    i.explicit_locations = true;
    i.nl = 2;
    uint32_t loc[][6] = {{0, 4, 0, 1, 1, 0}, {4, 4, 0, 2, 1, 1}};
    memcpy(i.locations, loc, sizeof(loc));
    yyjson_mut_doc *fm =
        changed(r,
                c->base,
                "functions",
                "[{\"functionId\":1,\"file\":0,\"instruction\":1,\"parameters\":[]}]");
    code = encode(r, i);
    check(r, c, fm, code, OK, 0);
    check(r, c, changed(r, fm, "externals", "{\"x\":{\"mutable\":false}}"), code, EXTERNALS, 0);
}

static void reserved_cases(scenario_run_t *r, configuration_t *c) {
    text_t names[] = {{"__main__", 8}, {"__dir__", 7}};
    unsigned lexical[] = {16, 17, 18, 79}, properties[] = {13, 15, 19, 20, 77};
    for (size_t k = 0; k < N(names); k++) {
        for (size_t j = 0; j < N(lexical); j++) {
            image_t i = two(lexical[j], 0);
            i.ns = 1;
            i.strings[0] = names[k];
            check(r, c, c->base, encode(r, i), IMAGE, 0);
        }
        for (size_t j = 0; j < N(properties); j++) {
            image_t i = two(properties[j], 0);
            i.ns = 1;
            i.strings[0] = names[k];
            check(r, c, c->base, encode(r, i), OK, 0);
        }
        for (unsigned op = 69; op <= 70; op++) {
            image_t i = two(op, 1);
            i.ns = 1;
            i.strings[0] = names[k];
            i.nd = 1;
            check(r, c, c->base, encode(r, i), IMAGE, 0);
        }
        instruction_t end = instruction(2, 0);
        image_t i = basic(&end, 1);
        i.ns = 1;
        i.strings[0] = names[k];
        i.nv = 1;
        i.lengths[0] = 1;
        check(r, c, c->base, encode(r, i), IMAGE, 0);
    }
    text_t legal[] = {{"__main__x", 9}, {"__dir__\0x", 9}};
    for (size_t k = 0; k < N(legal); k++) {
        image_t i = two(16, 0);
        i.ns = 1;
        i.strings[0] = legal[k];
        check(r, c, c->base, encode(r, i), OK, 0);
    }
}

static void corrupt_cases(scenario_run_t *r, configuration_t *c) {
    unsigned ops[] = {255, 3, 73, 74, 13, 0}, args[] = {0, MAX, 1, MAX, 0, 1};
    for (size_t k = 0; k < N(ops); k++)
        check(r, c, c->base, encode(r, two(ops[k], args[k])), IMAGE, 0);
    size_t lengths[] = {0, 8, c->minimal.size - 1};
    for (size_t k = 0; k < N(lengths); k++)
        check(r, c, c->base, (bytes_t){c->minimal.data, lengths[k]}, IMAGE, 0);
    bytes_t extra = {owned(r, calloc(1, c->minimal.size + 1)), c->minimal.size + 1};
    memcpy(extra.data, c->minimal.data, c->minimal.size);
    check(r, c, c->base, extra, IMAGE, 0);
    size_t n;
    char *raw = yyjson_mut_write(c->base, 0, &n);
    owned(r, raw);
    const char *needle = "\"main\":0";
    char *at = strstr(raw, needle);
    scenario_require(r, at != NULL, "main fixture token");
    const char *bad[] = {"0e0", "0.0", "\"0\"", "4294967296"};
    for (size_t k = 0; k < N(bad); k++) {
        size_t prefix = (size_t)(at - raw) + 7, len = strlen(bad[k]);
        bytes_t j = {owned(r, malloc(n + len)), n - 1 + len};
        memcpy(j.data, raw, prefix);
        memcpy(j.data + prefix, bad[k], len);
        memcpy(j.data + prefix + len, raw + prefix + 1, n - prefix - 1);
        check_raw(r, c, j, c->minimal, JSON, 0);
    }
}

static void body(scenario_run_t *r, void *user) {
    configuration_t *c = user;
    const char *args[] = {c->exe, NULL};
    child_options_t options = {.executable = c->exe, .arguments = args};
    scenario_command(r, &options, 0);
    bytes_t json = read_file(r, c, "empty.module.json");
    c->base = parse(r, json.data, json.size);
    c->minimal = read_file(r, c, "empty.bytecode.bin");
    files_cases(r, c);
    functions_cases(r, c);
    imports_cases(r, c);
    assignment_cases(r, c);
    external_cases(r, c);
    reserved_cases(r, c);
    corrupt_cases(r, c);
    scenario_require(r, c->count == 182, "complete legacy case count");
    printf("Module bytecode validation: %zu cases passed\n", c->count);
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
        puts("integration.module_bytecode");
        result = 0;
    } else if (argc == 6 || argc == 7) {
        fs_error_t error = {0};
        char *exe = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        char *vectors = absolute_fs_path(argv[5], &error);
        clear_fs_error(&error);
        if (exe && vectors) {
            configuration_t c = {.exe = exe, .vectors = vectors};
            scenario_definition_t definition = {"integration.module_bytecode",
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
        fputs("usage: module_bytecode_scenarios ID HOST BUILD DATA VECTORS [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
