/** @file module_access_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent basic module-access fixtures (#358).
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
    unsigned group;
} configuration_t;

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

/* Independent GOATBC01 writer: no production serializer or opcode constants.
 * File order is 0..count-1, as in every original Python invocation. */
static void fixture(scenario_run_t *r,
                    configuration_t *c,
                    unsigned count,
                    unsigned padding,
                    int modifier,
                    bool constant) {
    size_t ni = count * (padding + 3), nd = modifier < 0 ? 1 : count + 1;
    size_t od = 64 + ni * 8, of = od + nd * 16, on = of + count * 16;
    size_t start = on + count * 24, payload = 8 + (nd - 1) * 16;
    size_t size = start + payload;
    unsigned char *code = owned(r, calloc(1, size));
    memcpy(code, "GOATBC01", 8);
    uint32_t header[] = {1,
                         64,
                         (uint32_t)size,
                         (uint32_t)ni,
                         (uint32_t)nd,
                         count,
                         0,
                         count,
                         64,
                         (uint32_t)od,
                         (uint32_t)of,
                         (uint32_t)on,
                         (uint32_t)on,
                         (uint32_t)start};
    for (size_t j = 0; j < N(header); j++)
        number(code + 8 + j * 4, header[j]);
    uint32_t name[] = {0, 8, 1, 1};
    for (size_t j = 0; j < N(name); j++)
        number(code + od + j * 4, name[j]);
    number(code + start, 'x');
    for (unsigned file = 0; file < count; file++) {
        uint32_t first = file * (padding + 3), entry = first + padding;
        code[64 + entry * 8] = 10;
        number(code + 68 + entry * 8, 100 + file);
        code[64 + (entry + 1) * 8] = modifier < 0 ? 16 : constant ? 70 : 69;
        if (modifier >= 0)
            number(code + 68 + (entry + 1) * 8, file + 1);
        code[64 + (entry + 2) * 8] = 2;
        uint32_t f[] = {first, padding + 3, entry, 0};
        uint32_t l[] = {first, padding + 3, file, 1, 1, 0};
        for (size_t j = 0; j < N(f); j++)
            number(code + of + file * 16 + j * 4, f[j]);
        for (size_t j = 0; j < N(l); j++)
            number(code + on + file * 24 + j * 4, l[j]);
        if (modifier >= 0) {
            uint32_t offset = 8 + file * 16;
            uint32_t d[] = {offset, 16, 3, 1};
            uint32_t declaration[] = {0x4445434c, 0, file, (uint32_t)modifier};
            for (size_t j = 0; j < N(d); j++) {
                number(code + od + (file + 1) * 16 + j * 4, d[j]);
                number(code + start + offset + j * 4, declaration[j]);
            }
        }
    }
    size_t n;
    const char *base = language_read(r, language_join(r, c->vectors, "empty.module.json"), &n);
    char error[256] = {0};
    yyjson_mut_doc *d = format_json_parse(base, n, error, sizeof(error));
    scenario_require(r, d != NULL, error);
    scenario_defer(r, free_doc, d);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(d), *files = yyjson_mut_arr(d);
#define SET(o, k, v) REQUIRE(yyjson_mut_obj_put(o, yyjson_mut_str(d, k), v))
    SET(root, "main", yyjson_mut_uint(d, count - 1));
    SET(root, "files", files);
    for (unsigned file = 0; file < count; file++) {
        char name[40];
        snprintf(name, sizeof(name), "absent-%u.goat", file);
        yyjson_mut_val *f = yyjson_mut_obj(d);
        SET(f, "name", yyjson_mut_strcpy(d, name));
        SET(f, "entry", yyjson_mut_uint(d, file * (padding + 3) + padding));
        SET(f, "external", yyjson_mut_bool(d, false));
        REQUIRE(yyjson_mut_arr_append(files, f));
    }
    if (modifier == 2) {
        yyjson_mut_val *externals = yyjson_mut_obj(d), *x = yyjson_mut_obj(d);
        SET(x, "mutable", yyjson_mut_bool(d, !constant));
        SET(externals, "x", x);
        SET(root, "externals", externals);
    }
#undef SET
    char *json = owned(r, yyjson_mut_write(d, 0, &n));
    unsigned char *input = owned(r, malloc(4 + n + size));
    number(input, (uint32_t)n);
    memcpy(input + 4, json, n);
    memcpy(input + 4 + n, code, size);
    char filename[96];
    snprintf(filename,
             sizeof(filename),
             "files-%u-pad-%u-modifier-%d-constant-%u.bin",
             count,
             padding,
             modifier,
             constant ? 1u : 0u);
    language_write(r,
                   language_join(r, scenario_work_directory(scenario_run_context(r)), filename),
                   input,
                   4 + n + size);
    const char *args[] = {c->exe, "wire", NULL};
    child_options_t options = {.executable = c->exe,
                               .arguments = args,
                               .stdin_data = input,
                               .stdin_size = 4 + n + size};
    language_check(r, scenario_command_capture(r, &options), 0, "", 0, "", 0);
}

static void body(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    if (c->group == 0) {
        const char *args[] = {c->exe, NULL};
        language_check(r, language_command(r, c->exe, args, NULL), 0, "", 0, "", 0);
    } else if (c->group == 1) {
        const unsigned counts[] = {1, 2, 3, 20};
        for (size_t j = 0; j < N(counts); j++)
            for (unsigned padding = 0; padding <= 2; padding += 2)
                fixture(r, c, counts[j], padding, -1, false);
    } else {
        for (int modifier = 0; modifier < 3; modifier++)
            for (unsigned constant = 0; constant < 2; constant++)
                for (unsigned padding = 0; padding <= 2; padding += 2)
                    fixture(r, c, 3, padding, modifier, constant != 0);
    }
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t ae = {0};
    if (!fs_arguments(&argc, &argv, &ae)) {
        clear_fs_error(&ae);
        return 1;
    }
#endif
    const char *ids[] = {"integration.module_access.model_vm",
                         "integration.module_access.wire_files",
                         "integration.module_access.wire_declarations"};
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
        fputs("usage: module_access_scenarios ID HOST BUILD DATA VECTORS [--keep-work]\n", stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
