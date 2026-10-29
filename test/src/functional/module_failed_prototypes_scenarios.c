/** @file module_failed_prototypes_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent failed-prototype fixtures for rules 41--45 (#357).
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
    unsigned shape;
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

/* Independent three-file image, nine UTF-32LE names and no functions.
 * Maximum 38 instructions includes five padding instructions per file. */
typedef struct {
    unsigned char payload[512];
    uint32_t descriptors[9][4], instructions[38][2], files[3][4], locations[3][6];
    size_t np, ni;
} image_t;

static void op(image_t *i, uint32_t code, uint32_t arg) {
    i->instructions[i->ni][0] = code;
    i->instructions[i->ni++][1] = arg;
}

static void fixture(scenario_run_t *r,
                    configuration_t *c,
                    unsigned primary,
                    unsigned padding,
                    bool failed,
                    const char *kind) {
    image_t *i = owned(r, calloc(1, sizeof(*i)));
    const char *names[] =
        {"child", "flatfn", "result", "error", "own", "seen", "payload", "clone", "copied"};
    for (size_t j = 0; j < N(names); j++) {
        size_t n = strlen(names[j]);
        i->np = (i->np + 7) & ~(size_t)7;
        uint32_t d[] = {(uint32_t)i->np, (uint32_t)(4 * (n + 1)), 1, (uint32_t)n};
        memcpy(i->descriptors[j], d, sizeof(d));
        for (size_t k = 0; k < n; k++)
            number(i->payload + i->np + 4 * k, (unsigned char)names[j][k]);
        i->np += 4 * (n + 1);
    }
    unsigned observer = (primary + 2) % 3;
    for (unsigned file = 0; file < 3; file++) {
        uint32_t first = (uint32_t)i->ni;
        for (unsigned j = 0; j < padding; j++)
            op(i, 0, 0);
        uint32_t entry = (uint32_t)i->ni;
        if (file == observer) {
            op(i, 15, 0);
            op(i, 6, 0);
            op(i, 19, 7);
            op(i, 49, 0);
            op(i, 18, 8);
            op(i, 5, 0);
            uint32_t handler = (uint32_t)i->ni + 8, end = handler + 2;
            op(i, 54, handler);
            op(i, 15, 0);
            op(i, 15, 1);
            op(i, 49, 0);
            op(i, 18, 2);
            op(i, 5, 0);
            op(i, 53, 0);
            op(i, 3, end);
            op(i, 18, 3);
            op(i, 5, 0);
            op(i, 15, 0);
            op(i, 19, 4);
            op(i, 18, 5);
            op(i, 5, 0);
        }
        op(i, 2, 0);
        uint32_t f[] = {first, (uint32_t)i->ni - first, entry, 0};
        memcpy(i->files[file], f, sizeof(f));
        uint32_t l[] = {first, (uint32_t)i->ni - first, file, 10 + file, 2, 0};
        memcpy(i->locations[file], l, sizeof(l));
    }
    size_t od = 64 + i->ni * 8, of = od + 9 * 16, on = of + 3 * 16, start = on + 3 * 24,
           size = start + i->np;
    unsigned char *code = owned(r, calloc(1, size));
    memcpy(code, "GOATBC01", 8);
    uint32_t header[] = {1,
                         64,
                         (uint32_t)size,
                         (uint32_t)i->ni,
                         9,
                         3,
                         0,
                         3,
                         64,
                         (uint32_t)od,
                         (uint32_t)of,
                         (uint32_t)on,
                         (uint32_t)on,
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
    TABLE(od, descriptors, 9, 4);
    TABLE(of, files, 3, 4);
    TABLE(on, locations, 3, 6);
#undef TABLE
    memcpy(code + start, i->payload, i->np);
    size_t n;
    const char *base = language_read(r, language_join(r, c->vectors, "empty.module.json"), &n);
    char error[256] = {0};
    yyjson_mut_doc *d = format_json_parse(base, n, error, sizeof(error));
    scenario_require(r, d != NULL, error);
    scenario_defer(r, free_doc, d);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(d), *files = yyjson_mut_arr(d);
#define SET(o, k, v) REQUIRE(yyjson_mut_obj_put(o, yyjson_mut_str(d, k), v))
    SET(root, "main", yyjson_mut_uint(d, primary));
    SET(root, "files", files);
    for (unsigned j = 0; j < 3; j++) {
        char name[40];
        snprintf(name, sizeof(name), "prototype-%u😀.goat", j);
        yyjson_mut_val *f = yyjson_mut_obj(d);
        SET(f, "name", yyjson_mut_strcpy(d, name));
        SET(f, "entry", yyjson_mut_uint(d, i->files[j][2]));
        SET(f, "external", yyjson_mut_bool(d, false));
        REQUIRE(yyjson_mut_arr_append(files, f));
    }
#undef SET
    char *json = owned(r, yyjson_mut_write(d, 0, &n));
    unsigned char *input = owned(r, malloc(4 + n + size));
    number(input, (uint32_t)n);
    memcpy(input + 4, json, n);
    memcpy(input + 4 + n, code, size);
    char filename[80], shape_text[16];
    snprintf(filename,
             sizeof(filename),
             "main-%u-pad-%u-failed-%u-%s.bin",
             primary,
             padding,
             failed ? 1u : 0u,
             kind);
    language_write(r,
                   language_join(r, scenario_work_directory(scenario_run_context(r)), filename),
                   input,
                   4 + n + size);
    snprintf(shape_text, sizeof(shape_text), "%u", c->shape);
    const char *args[] = {c->exe, shape_text, failed ? "1" : "0", kind, NULL};
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
    const unsigned padding[] = {0, 5};
    const char *kinds[] = {"object", "null"};
    for (unsigned primary = 0; primary < 3; primary++)
        for (size_t p = 0; p < N(padding); p++)
            for (unsigned failed = 0; failed < 2; failed++)
                for (size_t k = 0; k < N(kinds); k++)
                    fixture(r, c, primary, padding[p], failed != 0, kinds[k]);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t ae = {0};
    if (!fs_arguments(&argc, &argv, &ae)) {
        clear_fs_error(&ae);
        return 1;
    }
#endif
    const char *ids[] = {"integration.module_failed_prototypes.shape_0",
                         "integration.module_failed_prototypes.shape_1",
                         "integration.module_failed_prototypes.shape_2",
                         "integration.module_failed_prototypes.shape_3"};
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
                c[j] = (configuration_t){exe, vectors, (unsigned)j};
                defs[j] = (scenario_definition_t){ids[j], 120000, body, &c[j], NULL};
            }
            scenario_options_t o = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(defs, N(ids), argc == 7 ? 2 : 1, selection, &o);
        }
        FREE(exe);
        FREE(vectors);
    } else
        fputs(
            "usage: module_failed_prototypes_scenarios ID HOST BUILD DATA VECTORS [--keep-work]\n",
            stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
