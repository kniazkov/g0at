/** @file module_dependencies_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exact per-consumer dependency binding fixtures (#351).
 */
#ifndef _WIN32
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>
#define N(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    const char *exe, *vectors;
    size_t count;
    yyjson_mut_doc *schema;
} configuration_t;

typedef struct {
    const unsigned char *data;
    size_t size;
    const char *hash;
} bytes_t;

static bool free_mut(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool free_malloc(void *p) {
    free(p);
    return true;
}

static void *owned(scenario_run_t *r, void *p) {
    scenario_require(r, p != NULL, "allocation");
    scenario_defer(r, free_malloc, p);
    return p;
}

static yyjson_val *get(yyjson_val *v, const char *key) {
    return yyjson_obj_get(v, key);
}

static const char *string(scenario_run_t *r, yyjson_val *v) {
    scenario_require(r, yyjson_is_str(v), "JSON string");
    return yyjson_get_str(v);
}

static yyjson_val *parse(scenario_run_t *r, const void *s, size_t n) {
    yyjson_doc *d = yyjson_read(s, n, 0);
    scenario_require(r, d != NULL, "JSON response");
    scenario_defer(r, free_doc, d);
    return yyjson_doc_get_root(d);
}

static yyjson_mut_doc *mut(scenario_run_t *r, const char *s, size_t n) {
    char error[256] = {0};
    yyjson_mut_doc *d = format_json_parse(s, n, error, sizeof(error));
    scenario_require(r, d != NULL, error);
    scenario_defer(r, free_mut, d);
    return d;
}

static const char *path(scenario_run_t *r, const char *s) {
    return language_join(r, scenario_work_directory(scenario_run_context(r)), s);
}

static const char *join(scenario_run_t *r, const char *a, const char *b) {
    return language_join(r, a, b);
}

static const char *put(scenario_run_t *r, const char *p, const void *data, size_t n) {
    char *parent = language_owned(r, ALLOC(strlen(p) + 1));
    strcpy(parent, p);
    for (char *q = parent + 1; *q; q++)
        if (*q == '/') {
            *q = 0;
            fs_info_t info;
            fs_error_t e = {0};
            bool exists = inspect_fs_path(parent, true, &info, &e);
            clear_fs_error(&e);
            if (!exists)
                language_directory(r, parent);
            else
                scenario_require(r, info.kind == FS_DIRECTORY, "fixture parent");
            *q = '/';
        }
    language_write(r, p, data, n);
    return p;
}

static const char *empty(scenario_run_t *r, const char *p) {
    return put(r, p, "", 0);
}

static void number(unsigned char *p, uint32_t n) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (unsigned char)(n >> (8 * i));
}

static bytes_t archive(scenario_run_t *r,
                       configuration_t *c,
                       const char *name,
                       const char *version,
                       const char *file,
                       const char *dependencies) {
    unsigned char *image = owned(r, calloc(1, 208));
    memcpy(image, "GOATBC01", 8);
    const uint32_t header[] = {1, 64, 208, 6, 0, 1, 1, 2, 64, 112, 112, 128, 160, 208};
    for (size_t i = 0; i < N(header); i++)
        number(image + 8 + 4 * i, header[i]);
    const unsigned char ops[] = {1, 47, 5, 0, 7, 50};
    for (size_t i = 0; i < N(ops); i++)
        image[64 + i * 8] = ops[i];
    number(image + 68, 4);
    number(image + 116, 6);
    const uint32_t function[] = {1, 0, 1, 4, 0, 0, 0xffffffff, 0};
    for (size_t i = 0; i < N(function); i++)
        number(image + 128 + 4 * i, function[i]);
    const uint32_t locations[] = {0, 4, 0, 1, 1, 0, 4, 2, 0, 2, 1, 1};
    for (size_t i = 0; i < N(locations); i++)
        number(image + 160 + 4 * i, locations[i]);
    size_t n;
    const char *base = language_read(r, join(r, c->vectors, "empty.module.json"), &n);
    yyjson_mut_doc *d = mut(r, base, n);
    yyjson_mut_val *v = yyjson_mut_doc_get_root(d);
#define SET(obj, k, val)                                                                           \
    scenario_require(r, yyjson_mut_obj_put(obj, yyjson_mut_str(d, k), val), "metadata field")
    SET(v, "name", yyjson_mut_str(d, name));
    SET(v, "version", yyjson_mut_str(d, version));
    yyjson_mut_val *f = yyjson_mut_arr_get(yyjson_mut_obj_get(v, "files"), 0);
    SET(f, "name", yyjson_mut_str(d, file));
    const char *functions = "[{\"functionId\":1,\"file\":0,\"instruction\":1,\"parameters\":[]}]";
    yyjson_mut_doc *fd = mut(r, functions, strlen(functions));
    SET(v, "functions", yyjson_mut_val_mut_copy(d, yyjson_mut_doc_get_root(fd)));
    if (dependencies) {
        yyjson_mut_doc *dd = mut(r, dependencies, strlen(dependencies));
        SET(v, "dependencies", yyjson_mut_val_mut_copy(d, yyjson_mut_doc_get_root(dd)));
    }
    char *digest = owned(r, malloc(65)), error[256] = {0};
    scenario_require(r,
                     format_binary_main_hash(v,
                                             yyjson_mut_doc_get_root(c->schema),
                                             image,
                                             208,
                                             digest,
                                             error,
                                             sizeof(error)),
                     error);
    SET(v, "mainHash", yyjson_mut_str(d, digest));
#undef SET
    char *json = owned(r, yyjson_mut_write(d, 0, &n));
    format_zip_entry_t entries[2];
    format_zip_entry_init(&entries[0], "module.json", json, n, false);
    format_zip_entry_init(&entries[1], "bytecode.bin", image, 208, false);
    size_t size;
    unsigned char *zip = format_zip_build(entries, 2, NULL, 0, false, &size, error, sizeof(error));
    scenario_require(r, zip != NULL, error);
    owned(r, zip);
    return (bytes_t){zip, size, digest};
}

typedef struct {
    const char *path, *json;
} module_t;

static const char *list(scenario_run_t *r, const char *a, const char *b) {
    size_t n = (a ? strlen(a) : 0) + (b ? strlen(b) : 0) + 4;
    char *s = owned(r, malloc(n));
    snprintf(s, n, "[%s%s%s]", a ? a : "", b ? "," : "", b ? b : "");
    return s;
}

static module_t install(scenario_run_t *r,
                        configuration_t *c,
                        const char *project,
                        const char *name,
                        const char *version,
                        const char *deps,
                        const char *file) {
    bytes_t b = archive(r, c, name, version, file ? file : "main.goat", deps);
    size_t n = strlen(name) + strlen(version) + 128;
    char *record = owned(r, malloc(n)), *filename = owned(r, malloc(strlen(version) + 6));
    snprintf(record,
             n,
             "{\"name\":\"%s\",\"version\":\"%s\",\"hash\":\"%s\"}",
             name,
             version,
             b.hash);
    sprintf(filename, "%s.gbin", version);
    const char *p = join(r, join(r, join(r, project, "modules"), name), filename);
    put(r, p, b.data, b.size);
    return (module_t){p, record};
}

static void describe(scenario_run_t *r, const char *desc, const char *deps) {
    const char *base =
        "{\"formatVersion\":1,\"name\":\"app\",\"version\":\"1\",\"main\":\"main.goat\"";
    size_t n = strlen(base) + (deps ? strlen(deps) : 0) + 32;
    char *s = owned(r, malloc(n));
    snprintf(s, n, "%s%s%s}", base, deps ? ",\"dependencies\":" : "", deps ? deps : "");
    put(r, desc, s, strlen(s));
}

static void source(scenario_run_t *r, const char *p, const char *s) {
    put(r, p, s, strlen(s));
}

static void integer(scenario_run_t *r, yyjson_val *v, const char *key, int expected) {
    yyjson_val *a = get(v, key);
    scenario_require(r, yyjson_is_int(a) && yyjson_get_int(a) == expected, key);
}

static yyjson_val *run(scenario_run_t *r,
                       configuration_t *c,
                       const char *mode,
                       const char *root,
                       const char *consumer,
                       int status,
                       int search_status) {
    const char *args[] = {c->exe, mode, root, consumer ? consumer : root, NULL};
    scenario_command_result_t out = language_command(r, c->exe, args, NULL);
    c->count++;
    scenario_require(r, out.exit_code == 0, "dependency host exit");
    yyjson_val *v = parse(r, out.out, out.out_size);
    integer(r, v, "status", status);
    if (status) {
        integer(r, v, "search_status", search_status);
        scenario_require(r, get(v, "dependencies") == NULL, "no partial dependencies on failure");
    }
    return v;
}

static void records(scenario_run_t *r, yyjson_val *v, const char *a, const char *b) {
    yyjson_val *actual = get(v, "dependencies");
    const char *json = list(r, a, b);
    yyjson_val *expected = parse(r, json, strlen(json));
    scenario_require(r, yyjson_equals(actual, expected), "exact canonical dependency records");
}

static void body(scenario_run_t *r, void *user) {
    configuration_t *c = user;
    size_t n;
    const char *schema = language_read(r, join(r, c->vectors, "../schema-source.json"), &n);
    c->schema = mut(r, schema, n);
    const char *api[] = {c->exe, NULL};
    scenario_require(r,
                     language_command(r, c->exe, api, NULL).exit_code == 0,
                     "dependency API checks");
    const char *project = path(r, "проект");
    const char *desc = empty(r, join(r, project, "app.gmod"));
    const char *main = empty(r, join(r, project, "main.goat"));
    module_t math1 = install(r, c, project, "math", "1", NULL, NULL);
    module_t math2 = install(r, c, project, "math", "2", NULL, NULL);
    module_t lib = install(r, c, project, "library", "7", list(r, math1.json, NULL), NULL);
#define RUN(mode, root, consumer) run(r, c, mode, root, consumer, 0, 0)
#define RECS(v, a, b) records(r, v, a, b)
    describe(r, desc, "{\"math\":\"2\",\"library\":\"7\",\"never-installed\":\"4\"}");
    source(r,
           main,
           "import \"library\"; if(false){import \"math\";} import \"math@1\"; import \"math\";");
    yyjson_val *app = RUN("gmod", desc, NULL);
    /* Three ordered records are checked separately from the two-record helper. */
    const char *pair = list(r, math1.json, math2.json);
    size_t z = strlen(pair) + strlen(lib.json) + 3;
    char *three = owned(r, malloc(z));
    snprintf(three, z, "[%s,%s", lib.json, pair + 1);
    scenario_require(r,
                     yyjson_equals(get(app, "dependencies"), parse(r, three, strlen(three))),
                     "three direct bindings");
    integer(r, app, "unused", 1);
    RECS(RUN("module", desc, lib.path), math1.json, NULL);
    RECS(RUN("module", desc, math1.path), NULL, NULL);
    RECS(RUN("module", desc, math2.path), NULL, NULL);
    RECS(yyjson_arr_get(get(app, "snapshots"), 0), math1.json, NULL);
    describe(r, desc, "{\"math\":\"not-installed\"}");
    source(r, main, "import \"math@1\"; import \"math@1\";");
    yyjson_val *v = RUN("gmod", desc, NULL);
    RECS(v, math1.json, NULL);
    integer(r, v, "unused", 1);
    describe(r, desc, "{\"math\":\"2\"}");
    source(r, main, "include \"own.goat\"; import \"math@1\";");
    source(r, join(r, project, "own.goat"), "func(){import \"math\";}");
    RECS(RUN("gmod", desc, NULL), math1.json, math2.json);
    describe(r, desc, NULL);
    source(r, main, "import \"math@1\"; import \"math\";");
    run(r, c, "gmod", desc, NULL, 105, 0);
    describe(r, desc, "{\"library\":\"7\"}");
    source(r, main, "import \"library\";");
    app = RUN("gmod", desc, NULL);
    RECS(app, lib.json, NULL);
    RECS(yyjson_arr_get(get(app, "snapshots"), 0), math1.json, NULL);
    language_remove(r, math1.path);
    RECS(RUN("gmod", desc, NULL), lib.json, NULL);
    run(r, c, "module", desc, lib.path, 2, 3);
    math1 = install(r, c, project, "math", "1", NULL, NULL);
    module_t application =
        install(r, c, project, "application", "1", list(r, lib.json, NULL), NULL);
    RECS(RUN("module", desc, application.path), lib.json, NULL);
    RECS(RUN("module", desc, lib.path), math1.json, NULL);
    source(r, math1.path, "not an archive");
    run(r, c, "module", desc, lib.path, 2, 4);
    math1 = install(r, c, project, "math", "1", NULL, NULL);
    module_t replacement = install(r, c, project, "math", "1", NULL, "replacement.goat");
    scenario_require(r, strcmp(replacement.json, math1.json) != 0, "changed build hash");
    describe(r, desc, "{\"math\":\"1\"}");
    source(r, main, "import \"math\";");
    RECS(RUN("gmod", desc, NULL), replacement.json, NULL);
    v = run(r, c, "module", desc, lib.path, 2, 6);
    integer(r, v, "index", 0);
    yyjson_val *attempted = get(v, "attempted");
    scenario_require(r,
                     yyjson_is_arr(attempted) && yyjson_arr_size(attempted) == 1,
                     "one failed hash candidate");
    const char *p = string(r, yyjson_arr_get(attempted, 0));
    size_t len = strlen(p);
    scenario_require(
        r,
        len >= 11 && (len == 11 || p[len - 12] == '/' || p[len - 12] == '\\')
            && (!strcmp(p + len - 11, "math/1.gbin") || !strcmp(p + len - 11, "math\\1.gbin")),
        "hash failure path");
    module_t second = install(r, c, project, "second", "1", list(r, replacement.json, NULL), NULL);
    RECS(RUN("module", desc, second.path), replacement.json, NULL);
    run(r, c, "module", desc, lib.path, 2, 6);
    module_t unordered =
        install(r, c, project, "unordered", "1", list(r, math2.json, replacement.json), NULL);
    module_t ordered =
        install(r, c, project, "ordered", "1", list(r, replacement.json, math2.json), NULL);
    RECS(RUN("module", desc, unordered.path), replacement.json, math2.json);
    RECS(RUN("module", desc, ordered.path), replacement.json, math2.json);
    const char *missing = "{\"name\":\"zmissing\",\"version\":\"1\",\"hash\":"
                          "\"0000000000000000000000000000000000000000000000000000000000000000\"}";
    module_t partial =
        install(r, c, project, "partial", "1", list(r, replacement.json, missing), NULL);
    v = run(r, c, "module", desc, partial.path, 2, 3);
    integer(r, v, "index", 1);
    describe(r, desc, "{\"missing\":\"1\"}");
    source(r, main, "");
    v = RUN("gmod", desc, NULL);
    RECS(v, NULL, NULL);
    integer(r, v, "unused", 1);
    source(r, main, "import \"math@2\"; import \"math@1\";");
    RECS(RUN("source", main, NULL), replacement.json, math2.json);
    scenario_require(r, c->count == 22, "all original checks executed");
    printf("Per-consumer dependency bindings: %zu checks passed\n", c->count);
#undef RUN
#undef RECS
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t arg_error = {0};
    if (!fs_arguments(&argc, &argv, &arg_error)) {
        clear_fs_error(&arg_error);
        return 1;
    }
#endif
    int result = 1;
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        puts("integration.module_dependencies");
        result = 0;
    } else if (argc == 6 || argc == 7) {
        fs_error_t e = {0};
        char *exe = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        char *vectors = absolute_fs_path(argv[5], &e);
        clear_fs_error(&e);
        if (exe && vectors) {
            configuration_t c = {.exe = exe, .vectors = vectors};
            scenario_definition_t def = {"integration.module_dependencies", 180000, body, &c, NULL};
            scenario_options_t o = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(&def, 1, argc == 7 ? 2 : 1, selection, &o);
        }
        FREE(exe);
        FREE(vectors);
    } else
        fputs("usage: module_dependencies_scenarios ID HOST BUILD DATA VECTORS [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
