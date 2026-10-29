/** @file module_incomplete_scopes_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent incomplete-scope fixtures for rules 36--40 (#357).
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

/* Bounded independent image: two files, eleven names, two declarations and
 * at most 40 instructions. No production bytecode writer is used. */
typedef struct {
    unsigned char payload[512];
    uint32_t descriptors[13][4], instructions[48][2], files[2][4], locations[2][6];
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

static void fixture(scenario_run_t *r,
                    configuration_t *c,
                    bool caught,
                    unsigned primary,
                    unsigned padding,
                    bool variant) {
    image_t *i = owned(r, calloc(1, sizeof(*i)));
    const char *names[] = {"x",
                           "secret",
                           "target",
                           "seen",
                           "child",
                           "own",
                           "caught",
                           "after",
                           "clone",
                           "flat",
                           "error"};
    for (size_t j = 0; j < N(names); j++)
        string(i, names[j]);
    unsigned char declaration[16];
    number(declaration, 0x4445434c);
    number(declaration + 4, 0);
    number(declaration + 8, primary);
    number(declaration + 12, 1);
    uint32_t dx = data(i, declaration, sizeof(declaration), 3, 1);
    number(declaration + 4, 1);
    number(declaration + 12, 4);
    uint32_t ds = data(i, declaration, sizeof(declaration), 3, 1);
    unsigned rule = c->rule;
    for (unsigned file = 0; file < 2; file++) {
        uint32_t first = (uint32_t)i->ni;
        for (unsigned j = 0; j < padding; j++)
            op(i, 0, 0);
        uint32_t entry = (uint32_t)i->ni;
        if (file == primary) {
            op(i, 10, 10);
            op(i, variant ? 70 : 69, dx);
            op(i, 10, 55);
            op(i, 69, ds);
        } else {
            op(i, 15, 2);
            op(i, 64, 0);
            op(i, 16, 3);
            if (rule == 40) {
                op(i, 15, 2);
                /* The high word encodes the independent 16-bit operand. */
                op(i, 59 | (1u << 16), 1);
                op(i, 10, 5);
                op(i, 16, 5);
                op(i, 52, 0);
                op(i, 16, 4);
            }
            uint32_t length = rule == 37 ? 4 : rule == 39 ? 5 : 3;
            uint32_t handler = (uint32_t)i->ni + 1 + length + 2;
            if (caught)
                op(i, 54, handler);
            if (rule == 37) {
                op(i, 10, 20);
                op(i, 15, 2);
                op(i, 20, 0);
            } else if (rule == 39) {
                op(i, 15, 2);
                op(i, 6, 0);
                op(i, 19, variant ? 9 : 8);
                op(i, 49, 0);
            } else {
                op(i, 15, rule == 40 ? 4 : 2);
                op(i, 19, 0);
            }
            op(i, 5, 0);
            if (caught) {
                op(i, 53, 0);
                op(i, 3, handler + 6);
                op(i, 18, 10);
                op(i, 5, 0);
                op(i, 15, 6);
                op(i, 23, 0);
                op(i, 18, 6);
                op(i, 5, 0);
            }
            op(i, 15, 7);
            op(i, 23, 0);
            op(i, 18, 7);
            op(i, 5, 0);
        }
        op(i, 2, 0);
        uint32_t f[] = {first, (uint32_t)i->ni - first, entry, 0};
        memcpy(i->files[file], f, sizeof(f));
        uint32_t l[] = {first, (uint32_t)i->ni - first, file, 10 + file, 2, 0};
        memcpy(i->locations[file], l, sizeof(l));
    }
    size_t od = 64 + i->ni * 8, of = od + i->nd * 16, on = of + 2 * 16, ol = on,
           start = ol + 2 * 24, size = start + i->np;
    unsigned char *code = owned(r, calloc(1, size));
    memcpy(code, "GOATBC01", 8);
    uint32_t header[] = {1,
                         64,
                         (uint32_t)size,
                         (uint32_t)i->ni,
                         (uint32_t)i->nd,
                         2,
                         0,
                         2,
                         64,
                         (uint32_t)od,
                         (uint32_t)of,
                         (uint32_t)on,
                         (uint32_t)ol,
                         (uint32_t)start};
    for (size_t j = 0; j < N(header); j++)
        number(code + 8 + 4 * j, header[j]);
    for (size_t j = 0; j < i->ni; j++) {
        number(code + 64 + 8 * j, i->instructions[j][0]);
        number(code + 68 + 8 * j, i->instructions[j][1]);
    }
#define TABLE(at, field, count, width)                                                             \
    for (size_t j = 0; j < (count); j++)                                                           \
        for (size_t k = 0; k < (width); k++)                                                       \
            number(code + (at) + j * (width) * 4 + k * 4, i->field[j][k]);
    TABLE(od, descriptors, i->nd, 4);
    TABLE(of, files, 2, 4);
    TABLE(ol, locations, 2, 6);
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
    SET(root, "files", files);
    SET(root, "main", yyjson_mut_uint(d, primary));
    for (unsigned j = 0; j < 2; j++) {
        char name[32];
        snprintf(name, sizeof(name), "scope-%u😀.goat", j);
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
    char filename[80], rule_text[16];
    snprintf(filename,
             sizeof(filename),
             "caught-%u-main-%u-pad-%u-variant-%u.bin",
             caught ? 1u : 0u,
             primary,
             padding,
             variant ? 1u : 0u);
    language_write(r,
                   language_join(r, scenario_work_directory(scenario_run_context(r)), filename),
                   input,
                   4 + n + size);
    snprintf(rule_text, sizeof(rule_text), "%u", rule);
    const char *args[] = {c->exe, rule_text, caught ? "1" : "0", variant ? "1" : "0", NULL};
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
    const unsigned padding[] = {0, 4};
    for (unsigned caught = 0; caught < 2; caught++)
        for (unsigned primary = 0; primary < 2; primary++)
            for (size_t p = 0; p < N(padding); p++)
                for (unsigned variant = 0; variant < 2; variant++)
                    fixture(r, c, caught != 0, primary, padding[p], variant != 0);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t ae = {0};
    if (!fs_arguments(&argc, &argv, &ae)) {
        clear_fs_error(&ae);
        return 1;
    }
#endif
    const char *ids[] = {"integration.module_incomplete_scopes.rule_36",
                         "integration.module_incomplete_scopes.rule_37",
                         "integration.module_incomplete_scopes.rule_38",
                         "integration.module_incomplete_scopes.rule_39",
                         "integration.module_incomplete_scopes.rule_40"};
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
                c[j] = (configuration_t){exe, vectors, (unsigned)j + 36};
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
            "usage: module_incomplete_scopes_scenarios ID HOST BUILD DATA VECTORS [--keep-work]\n",
            stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
