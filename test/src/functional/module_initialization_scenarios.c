/** @file module_initialization_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief One-shot initialization, interruption and saved failure fixtures (#355).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_json.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>
#define N(a) (sizeof(a) / sizeof((a)[0]))
#define REQUIRE(x) scenario_require(r, (x), #x)

typedef struct {
    const char *exe, *vectors;
    const char *mode;
    bool errors;
} configuration_t;

/* yyjson and the independent wire builder use the C allocator, not ALLOC. */
static bool free_malloc(void *p) {
    free(p);
    return true;
}

static void *owned(scenario_run_t *r, void *p) {
    REQUIRE(p != NULL);
    scenario_defer(r, free_malloc, p);
    return p;
}

static bool free_doc(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static void number(unsigned char *p, uint32_t n) {
    for (unsigned j = 0; j < 4; j++)
        p[j] = (unsigned char)(n >> (8 * j));
}

/* Independent wire construction, matching the former Python fixture. The fixed
 * three-file input uses 21 descriptors, 3 functions, 6 locations and at most
 * 114 instructions (caught mode, padding <= 7); declarations and strings fit in 512 bytes. */
typedef struct {
    unsigned char payload[512];
    uint32_t descriptors[21][4], instructions[114][2], files[3][4], functions[3][8],
        locations[6][6];
    size_t np, nd, ni;
} image_t;

static uint32_t data(image_t *i, const void *p, size_t n, uint32_t kind, uint32_t count) {
    i->np = (i->np + 7) & ~(size_t)7;
    uint32_t id = (uint32_t)i->nd++;
    uint32_t d[] = {(uint32_t)i->np, (uint32_t)n, kind, count};
    memcpy(i->descriptors[id], d, sizeof(d));
    memcpy(i->payload + i->np, p, n);
    i->np += n;
    return id;
}

static uint32_t string(image_t *i, const char *s) {
    unsigned char bytes[32] = {0};
    size_t n = strlen(s);
    for (size_t j = 0; j < n; j++)
        number(bytes + 4 * j, (unsigned char)s[j]);
    return data(i, bytes, 4 * (n + 1), 1, (uint32_t)n);
}

static void op(image_t *i, uint32_t code, uint32_t arg) {
    i->instructions[i->ni][0] = code;
    i->instructions[i->ni++][1] = arg;
}

static void
fixture(scenario_run_t *r, configuration_t *c, unsigned padding, unsigned main, bool external) {
    image_t *i = owned(r, calloc(1, sizeof(*i)));
    uint32_t x = string(i, "x"), constant = string(i, "c"), nested = string(i, "nested");
    uint32_t error_name = string(i, "error");
    uint32_t inner = string(i, "inner"), cleanup = string(i, "cleanup");
    for (unsigned file = 0; file < 3; file++) {
        uint32_t names[] = {x, constant, nested, inner, cleanup}, declarations[5];
        for (size_t j = 0; j < N(names); j++) {
            unsigned char declaration[16];
            number(declaration, 0x4445434c);
            number(declaration + 4, names[j]);
            number(declaration + 8, file);
            number(declaration + 12, 1);
            declarations[j] = data(i, declaration, sizeof(declaration), 3, 1);
        }
        uint32_t first = (uint32_t)i->ni;
        for (unsigned j = 0; j < padding; j++)
            op(i, 0, 0);
        uint32_t entry = (uint32_t)i->ni;
        if (!c->errors && !strcmp(c->mode, "early")) {
            op(i, 71, declarations[0]);
            op(i, 5, 0);
        } else if (!c->errors && !strcmp(c->mode, "return")) {
            op(i, 7, 0);
            op(i, 50, 0);
        } else if (!c->errors && !strcmp(c->mode, "caught")) {
            op(i, 54, entry + 5);
            op(i, 71, declarations[0]);
            op(i, 5, 0);
            op(i, 53, 0);
            op(i, 3, entry + 6);
            op(i, 16, error_name);
        }
        uint32_t final = (uint32_t)i->ni;
        op(i, 66, final + 3);
        op(i, 0, 0);
        op(i, 67, final + 6);
        op(i, 10, 9);
        op(i, 69, declarations[4]);
        op(i, 68, 0);
        op(i, 10, 100 + file);
        op(i, 69, declarations[0]);
        op(i, 10, 7);
        op(i, 70, declarations[1]);
        op(i, 71, declarations[0]);
        op(i, 5, 0);
        op(i, 51, 0);
        op(i, 10, 8);
        op(i, 69, declarations[2]);
        op(i, 52, 0);
        op(i, 5, 0);
        uint32_t definition = (uint32_t)i->ni, function_body = definition + 4;
        op(i, 1, function_body);
        op(i, 47, 0);
        op(i, 5, 0);
        op(i, 2, 0);
        op(i, 10, 15);
        op(i, 69, declarations[3]);
        op(i, 7, 0);
        op(i, 50, 0);
        uint32_t f[] = {file + 1, file, definition + 1, function_body, 0, 0, 0xffffffff, 0};
        memcpy(i->functions[file], f, sizeof(f));
        uint32_t record[] = {first, (uint32_t)i->ni - first, entry, 0};
        memcpy(i->files[file], record, sizeof(record));
        uint32_t location[] = {first, function_body - first, file, 1, 1, 0};
        memcpy(i->locations[2 * file], location, sizeof(location));
        uint32_t location2[] =
            {function_body, (uint32_t)i->ni - function_body, file, 2, 1, file + 1};
        memcpy(i->locations[2 * file + 1], location2, sizeof(location2));
    }
    size_t od = 64 + i->ni * 8, of = od + i->nd * 16, on = of + 3 * 16, ol = on + 3 * 32,
           start = ol + 6 * 24, size = start + i->np;
    unsigned char *code = owned(r, calloc(1, size));
    memcpy(code, "GOATBC01", 8);
    uint32_t header[] = {1,
                         64,
                         (uint32_t)size,
                         (uint32_t)i->ni,
                         (uint32_t)i->nd,
                         3,
                         3,
                         6,
                         64,
                         (uint32_t)od,
                         (uint32_t)of,
                         (uint32_t)on,
                         (uint32_t)ol,
                         (uint32_t)start};
    for (size_t j = 0; j < N(header); j++)
        number(code + 8 + 4 * j, header[j]);
    for (size_t j = 0; j < i->ni; j++) {
        code[64 + 8 * j] = (unsigned char)i->instructions[j][0];
        number(code + 68 + 8 * j, i->instructions[j][1]);
    }
#define TABLE(at, field, count, width)                                                             \
    for (size_t j = 0; j < (count); j++)                                                           \
        for (size_t k = 0; k < (width); k++)                                                       \
            number(code + (at) + j * (width) * 4 + k * 4, i->field[j][k]);
    TABLE(od, descriptors, i->nd, 4);
    TABLE(of, files, 3, 4);
    TABLE(on, functions, 3, 8);
    TABLE(ol, locations, 6, 6);
#undef TABLE
    memcpy(code + start, i->payload, i->np);
    size_t n;
    const char *base = language_read(r, language_join(r, c->vectors, "empty.module.json"), &n);
    char error[256] = {0};
    yyjson_mut_doc *d = format_json_parse(base, n, error, sizeof(error));
    scenario_require(r, d != NULL, error);
    scenario_defer(r, free_doc, d);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(d), *files = yyjson_mut_arr(d),
                   *functions = yyjson_mut_arr(d);
#define SET(o, k, v) REQUIRE(yyjson_mut_obj_put(o, yyjson_mut_str(d, k), v))
    SET(root, "main", yyjson_mut_uint(d, main));
    SET(root, "files", files);
    SET(root, "functions", functions);
    for (unsigned j = 0; j < 3; j++) {
        char name[80];
        snprintf(name, sizeof(name), "не найден-%u😀.goat", j);
        yyjson_mut_val *f = yyjson_mut_obj(d);
        SET(f, "name", yyjson_mut_strcpy(d, external ? "same.goat" : name));
        SET(f, "entry", yyjson_mut_uint(d, i->files[j][2]));
        SET(f, "external", yyjson_mut_bool(d, external));
        REQUIRE(yyjson_mut_arr_append(files, f));
        f = yyjson_mut_obj(d);
        SET(f, "functionId", yyjson_mut_uint(d, i->functions[j][0]));
        SET(f, "file", yyjson_mut_uint(d, i->functions[j][1]));
        SET(f, "instruction", yyjson_mut_uint(d, i->functions[j][2]));
        SET(f, "parameters", yyjson_mut_arr(d));
        REQUIRE(yyjson_mut_arr_append(functions, f));
    }
#undef SET
    char *json = owned(r, yyjson_mut_write(d, 0, &n));
    unsigned char *input = owned(r, malloc(4 + n + size));
    number(input, (uint32_t)n);
    memcpy(input + 4, json, n);
    memcpy(input + 4 + n, code, size);
    char filename[80];
    snprintf(filename,
             sizeof(filename),
             "pad-%u-main-%u-external-%u.bin",
             padding,
             main,
             external ? 1u : 0u);
    language_write(r,
                   language_join(r, scenario_work_directory(scenario_run_context(r)), filename),
                   input,
                   4 + n + size);
    const char *args[] = {c->exe, c->mode, NULL};
    child_options_t options = {.executable = c->exe,
                               .arguments = args,
                               .stdin_data = input,
                               .stdin_size = 4 + n + size};
    scenario_command_result_t result = scenario_command_capture(r, &options);
    REQUIRE(result.exit_code == 0);
    scenario_require_bytes(r, filename, "OK\n", 3, result.out, result.out_size, true);
}

static void body(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const unsigned normal_padding[] = {0, 2, 7}, error_padding[] = {0, 3};
    const unsigned *padding = c->errors ? error_padding : normal_padding;
    size_t count = c->errors ? N(error_padding) : N(normal_padding);
    for (size_t p = 0; p < count; p++)
        for (unsigned main = 0; main < 2; main++)
            for (unsigned external = 0; external < (c->errors ? 1u : 2u); external++)
                fixture(r, c, padding[p], main, external != 0);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t ae = {0};
    if (!fs_arguments(&argc, &argv, &ae)) {
        clear_fs_error(&ae);
        return 1;
    }
#endif
    const char *ids[] = {"integration.module_initialization.normal",
                         "integration.module_initialization.caught",
                         "integration.module_initialization.early",
                         "integration.module_initialization.return",
                         "integration.module_initialization_errors.object",
                         "integration.module_initialization_errors.null",
                         "integration.module_initialization_errors.integer"};
    const char *modes[] = {"normal", "caught", "early", "return", "object", "null", "integer"};
    int result = 1;
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        for (size_t j = 0; j < N(ids); j++)
            puts(ids[j]);
        result = 0;
    } else if (argc == 6 || argc == 7) {
        fs_error_t e = {0};
        char *exe = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        char *vectors = absolute_fs_path(argv[5], &e);
        clear_fs_error(&e);
        if (exe && vectors) {
            configuration_t c[N(ids)];
            scenario_definition_t defs[N(ids)];
            for (size_t j = 0; j < N(ids); j++) {
                c[j] = (configuration_t){exe, vectors, modes[j], j >= 4};
                defs[j] = (scenario_definition_t){ids[j], 120000, body, &c[j], NULL};
            }
            scenario_options_t o = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(defs, N(ids), argc == 7 ? 2 : 1, selection, &o);
        }
        FREE(exe);
        FREE(vectors);
    } else
        fputs("usage: module_initialization_scenarios ID HOST BUILD DATA VECTORS [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
