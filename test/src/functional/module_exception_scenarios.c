/** @file module_exception_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent exception propagation fixtures for rules 26--35 (#356).
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
    unsigned rule;
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

/* Bounded independent wire image: three files, six names, one optional function.
 * Maximum padding is five instructions per file; no production bytecode writer. */
typedef struct {
    unsigned char payload[512];
    uint32_t descriptors[6][4], instructions[80][2], files[3][4], functions[1][8], locations[4][6];
    size_t np, nd, ni, nf, nl;
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
    unsigned char bytes[64] = {0};
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
fixture(scenario_run_t *r, configuration_t *c, unsigned padding, bool external, const char *kind) {
    image_t *i = owned(r, calloc(1, sizeof(*i)));
    const char *names[] = {"payload", "replacement", "cleanup", "caught", "function", "child"};
    for (size_t j = 0; j < N(names); j++)
        string(i, names[j]);
    unsigned rule = c->rule;
    for (unsigned file = 0; file < 3; file++) {
        uint32_t first = (uint32_t)i->ni;
        for (unsigned j = 0; j < padding; j++)
            op(i, 0, 0);
        uint32_t entry = (uint32_t)i->ni, body = 0;

        enum { SUCCESS, CATCH, REPLACE, THROW, FINALLY, FINALLY_REPLACE, FUNCTION } mode = SUCCESS;

        if ((rule >= 26 && rule <= 28 && file == 0) || (rule == 33 && file == 1))
            mode = rule == 33 ? REPLACE : CATCH;
        else if ((rule == 27 && file == 1)
                 || ((rule == 28 || rule == 32 || rule == 33) && file == 2))
            mode = THROW;
        else if ((rule == 30 || rule == 31) && file == 0)
            mode = THROW;
        else if ((rule == 34 || rule == 35) && file == 0)
            mode = rule == 34 ? FINALLY_REPLACE : FINALLY;
        else if (rule == 29 && file == 0)
            mode = FUNCTION;
        if (mode == CATCH || mode == REPLACE) {
            op(i, 54, entry + 5);
            op(i, 15, rule == 26 ? 0 : 5);
            op(i, rule == 26 ? 55 : 19, 0);
            op(i, 53, 0);
            op(i, 3, entry + (mode == REPLACE ? 12 : 10));
            op(i, 5, 0);
            op(i, 15, 3);
            op(i, 23, 0);
            op(i, 18, 3);
            op(i, 5, 0);
            if (mode == REPLACE) {
                op(i, 15, 1);
                op(i, 55, 0);
            }
        } else if (mode == THROW) {
            op(i, 15, 0);
            op(i, 55, 0);
        } else if (mode == FINALLY || mode == FINALLY_REPLACE) {
            op(i, 66, entry + 3);
            op(i, 15, 0);
            op(i, 55, 0);
            op(i, 15, 2);
            op(i, 23, 0);
            op(i, 18, 2);
            op(i, 5, 0);
            if (mode == FINALLY_REPLACE) {
                op(i, 15, 1);
                op(i, 55, 0);
            } else
                op(i, 68, 0);
        } else if (mode == FUNCTION) {
            body = entry + 4;
            op(i, 1, body);
            op(i, 47, 0);
            op(i, 16, 4);
            op(i, 2, 0);
            op(i, 15, 0);
            op(i, 55, 0);
            uint32_t f[] = {1, file, entry + 1, body, 0, 0, 0xffffffff, 0};
            memcpy(i->functions[i->nf++], f, sizeof(f));
        }
        op(i, 2, 0);
        uint32_t f[] = {first, (uint32_t)i->ni - first, entry, 0};
        memcpy(i->files[file], f, sizeof(f));
        if (mode == FUNCTION) {
            uint32_t l[] = {first, body - first, file, 10 + file, 3, 0};
            memcpy(i->locations[i->nl++], l, sizeof(l));
            uint32_t l2[] = {body, (uint32_t)i->ni - body, file, 20, 5, 1};
            memcpy(i->locations[i->nl++], l2, sizeof(l2));
        } else {
            uint32_t l[] = {first, (uint32_t)i->ni - first, file, 10 + file, 3, 0};
            memcpy(i->locations[i->nl++], l, sizeof(l));
        }
    }
    size_t od = 64 + i->ni * 8, of = od + i->nd * 16, on = of + 3 * 16, ol = on + i->nf * 32,
           start = ol + i->nl * 24, size = start + i->np;
    unsigned char *code = owned(r, calloc(1, size));
    memcpy(code, "GOATBC01", 8);
    uint32_t header[] = {1,
                         64,
                         (uint32_t)size,
                         (uint32_t)i->ni,
                         (uint32_t)i->nd,
                         3,
                         (uint32_t)i->nf,
                         (uint32_t)i->nl,
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
    TABLE(on, functions, i->nf, 8);
    TABLE(ol, locations, i->nl, 6);
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
    SET(root, "files", files);
    SET(root, "functions", functions);
    for (unsigned j = 0; j < 3; j++) {
        char name[32];
        snprintf(name, sizeof(name), "file-%u.goat", j);
        yyjson_mut_val *f = yyjson_mut_obj(d);
        SET(f, "name", yyjson_mut_strcpy(d, external ? "same😀.goat" : name));
        SET(f, "entry", yyjson_mut_uint(d, i->files[j][2]));
        SET(f, "external", yyjson_mut_bool(d, external));
        REQUIRE(yyjson_mut_arr_append(files, f));
    }
    for (size_t j = 0; j < i->nf; j++) {
        yyjson_mut_val *f = yyjson_mut_obj(d);
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
    char filename[80], rule_text[16];
    snprintf(filename,
             sizeof(filename),
             "pad-%u-external-%u-%s.bin",
             padding,
             external ? 1u : 0u,
             kind);
    language_write(r,
                   language_join(r, scenario_work_directory(scenario_run_context(r)), filename),
                   input,
                   4 + n + size);
    snprintf(rule_text, sizeof(rule_text), "%u", rule);
    const char *args[] = {c->exe, rule_text, kind, NULL};
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
    const char *kinds[] = {"object", "null", "integer"};
    for (size_t p = 0; p < N(padding); p++)
        for (unsigned external = 0; external < 2; external++)
            for (size_t k = 0; k < N(kinds); k++)
                fixture(r, c, padding[p], external != 0, kinds[k]);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t ae = {0};
    if (!fs_arguments(&argc, &argv, &ae)) {
        clear_fs_error(&ae);
        return 1;
    }
#endif
    const char *ids[] = {"integration.module_exception_propagation.rule_26",
                         "integration.module_exception_propagation.rule_27",
                         "integration.module_exception_propagation.rule_28",
                         "integration.module_exception_propagation.rule_29",
                         "integration.module_exception_propagation.rule_30",
                         "integration.module_exception_propagation.rule_31",
                         "integration.module_exception_propagation.rule_32",
                         "integration.module_exception_propagation.rule_33",
                         "integration.module_exception_propagation.rule_34",
                         "integration.module_exception_propagation.rule_35"};
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
                c[j] = (configuration_t){exe, vectors, (unsigned)j + 26};
                defs[j] = (scenario_definition_t){ids[j], 120000, body, &c[j], NULL};
            }
            scenario_options_t o = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(defs, N(ids), argc == 7 ? 2 : 1, selection, &o);
        }
        FREE(exe);
        FREE(vectors);
    } else
        fputs("usage: module_exception_scenarios ID HOST BUILD DATA VECTORS [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
