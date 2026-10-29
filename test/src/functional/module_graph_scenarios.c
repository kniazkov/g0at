/** @file module_graph_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Dependency graph preflight and diagnostics fixtures (#352).
 */
#ifndef _WIN32
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#include "test/support/format_fixtures.h"
#ifndef _WIN32
#    include <sys/resource.h>
#    include <unistd.h>
#endif
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>
#define N(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    const char *exe, *vectors, *self;
    size_t count;
    yyjson_mut_doc *schema;
} configuration_t;

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

static void number(unsigned char *p, uint32_t n) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (unsigned char)(n >> (8 * i));
}

#define REQUIRE(x) scenario_require(r, (x), #x)
#define ZERO "0000000000000000000000000000000000000000000000000000000000000000"
#define ONES "1111111111111111111111111111111111111111111111111111111111111111"

enum { SEARCH, HASH, CONFLICT, CYCLE, REFERENCE };

typedef struct {
    const char *path, *json;
    const unsigned char *data;
    size_t size;
    yyjson_mut_doc *metadata;
    const unsigned char *code;
    size_t code_size;
    bool native;
} module_t;

static void initialize(scenario_run_t *r, configuration_t *c) {
    if (!c->schema) {
        size_t n;
        const char *s = language_read(r, join(r, c->vectors, "../schema-source.json"), &n);
        c->schema = mut(r, s, n);
    }
}

static const char *list(scenario_run_t *r, const char *a, const char *b) {
    size_t n = (a ? strlen(a) : 0) + (b ? strlen(b) : 0) + 4;
    char *s = owned(r, malloc(n));
    snprintf(s, n, "[%s%s%s]", a ? a : "", a && b ? "," : "", b ? b : "");
    return s;
}

static const char *list3(scenario_run_t *r, const char *a, const char *b, const char *c) {
    size_t n = strlen(a) + strlen(b) + strlen(c) + 5;
    char *s = owned(r, malloc(n));
    snprintf(s, n, "[%s,%s,%s]", a, b, c);
    return s;
}

static void source(scenario_run_t *r, const char *p, const char *s) {
    put(r, p, s, strlen(s));
}

static void integer(scenario_run_t *r, yyjson_val *v, const char *k, int x) {
    scenario_require(r, yyjson_is_int(get(v, k)) && yyjson_get_int(get(v, k)) == x, k);
}

static module_t pack(scenario_run_t *r, module_t m) {
    size_t n;
    char *json = owned(r, yyjson_mut_write(m.metadata, 0, &n));
    static const char native[] = "Never load this opaque library during preflight";
    format_zip_entry_t e[3];
    format_zip_entry_init(e, "module.json", json, n, false);
    format_zip_entry_init(e + 1, "bytecode.bin", m.code, m.code_size, false);
    format_zip_entry_init(e + 2, "native/test.so", native, sizeof(native) - 1, false);
    for (size_t i = 0; i < 3; i++)
        e[i].flags = 0;
    char error[256] = {0};
    m.data =
        owned(r,
              format_zip_build(e, m.native ? 3 : 2, NULL, 0, false, &m.size, error, sizeof(error)));
    return m;
}

static module_t archive(scenario_run_t *r,
                        configuration_t *c,
                        const char *name,
                        const char *version,
                        const char *deps,
                        const char *refs,
                        bool native) {
    unsigned char image[208] = {0};
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
#define SET(o, k, x) REQUIRE(yyjson_mut_obj_put(o, yyjson_mut_str(d, k), x))
    SET(v, "name", yyjson_mut_strcpy(d, name));
    SET(v, "version", yyjson_mut_strcpy(d, version));
    const char *functions = "[{\"functionId\":1,\"file\":0,\"instruction\":1,\"parameters\":[]}]";
    SET(v,
        "functions",
        yyjson_mut_val_mut_copy(d, yyjson_mut_doc_get_root(mut(r, functions, strlen(functions)))));
    if (deps)
        SET(v,
            "dependencies",
            yyjson_mut_val_mut_copy(d, yyjson_mut_doc_get_root(mut(r, deps, strlen(deps)))));
    if (refs) {
        yyjson_mut_doc *rd = mut(r, refs, strlen(refs));
        yyjson_mut_val *external = yyjson_mut_obj(d);
        size_t i, max;
        yyjson_mut_val *k, *val;
        yyjson_mut_obj_foreach(yyjson_mut_doc_get_root(rd), i, max, k, val) {
            yyjson_mut_val *entry = yyjson_mut_obj(d);
            SET(entry, "mutable", yyjson_mut_false(d));
            SET(entry, "type", yyjson_mut_str(d, "function"));
            SET(entry, "functionRef", yyjson_mut_val_mut_copy(d, val));
            REQUIRE(yyjson_mut_obj_add(external, yyjson_mut_val_mut_copy(d, k), entry));
        }
        SET(v, "externals", external);
    }
    if (native) {
        static const char lib[] = "Never load this opaque library during preflight";
        char hash[65];
        format_binary_sha256(lib, sizeof(lib) - 1, hash);
        const char *target = language_read(r, join(r, c->vectors, "native-description.json"), &n);
        yyjson_mut_doc *td = mut(r, target, n);
        const char *spec = "[{\"functionId\":1,\"specializationId\":1,\"symbol\":\"goat_fn_1_v1\","
                           "\"signature\":{\"id\":\"goat.adapter.v1\",\"arguments\":[],\"result\":"
                           "\"null\"},\"when\":{\"argumentTypes\":[]}}]";
        yyjson_mut_val *a = yyjson_mut_arr(d), *entry = yyjson_mut_obj(d);
        SET(entry, "library", yyjson_mut_str(d, "native/test.so"));
        SET(entry, "hash", yyjson_mut_strcpy(d, hash));
        SET(entry, "goatAbiVersion", yyjson_mut_uint(d, 1));
        SET(entry,
            "target",
            yyjson_mut_val_mut_copy(d, yyjson_mut_obj_get(yyjson_mut_doc_get_root(td), "target")));
        SET(entry,
            "specializations",
            yyjson_mut_val_mut_copy(d, yyjson_mut_doc_get_root(mut(r, spec, strlen(spec)))));
        REQUIRE(yyjson_mut_arr_append(a, entry));
        SET(v, "native", a);
    }
    char error[256] = {0}, hash[65];
    size_t code_size;
    unsigned char *code = owned(r,
                                format_fixture_external_declarations(d,
                                                                     v,
                                                                     image,
                                                                     sizeof(image),
                                                                     &code_size,
                                                                     error,
                                                                     sizeof(error)));
    REQUIRE(format_binary_main_hash(v,
                                    yyjson_mut_doc_get_root(c->schema),
                                    code,
                                    code_size,
                                    hash,
                                    error,
                                    sizeof(error)));
    SET(v, "mainHash", yyjson_mut_strcpy(d, hash));
#undef SET
    char *record = owned(r, malloc(strlen(name) + strlen(version) + 128));
    sprintf(record, "{\"name\":\"%s\",\"version\":\"%s\",\"hash\":\"%s\"}", name, version, hash);
    return pack(r,
                (module_t){.json = record,
                           .metadata = d,
                           .code = code,
                           .code_size = code_size,
                           .native = native});
}

static module_t install(scenario_run_t *r,
                        configuration_t *c,
                        const char *project,
                        const char *name,
                        const char *version,
                        const char *deps,
                        const char *refs,
                        bool native) {
    module_t m = archive(r, c, name, version, deps, refs, native);
    char *file = owned(r, malloc(strlen(version) + 6));
    sprintf(file, "%s.gbin", version);
    m.path = join(r, join(r, join(r, project, "modules"), name), file);
    put(r, m.path, m.data, m.size);
    return m;
}

static module_t root_archive(scenario_run_t *r,
                             configuration_t *c,
                             const char *root,
                             const char *deps,
                             const char *refs,
                             bool native) {
    module_t m = archive(r, c, "app", "1", deps, refs, native);
    m.path = root;
    put(r, root, m.data, m.size);
    return m;
}

static module_t rewrite(scenario_run_t *r, configuration_t *c, module_t m, int kind) {
    (void)c;
    yyjson_mut_doc *d = m.metadata;
    yyjson_mut_val *v = yyjson_mut_doc_get_root(d);
#define SET(o, k, x) REQUIRE(yyjson_mut_obj_put(o, yyjson_mut_str(d, k), x))
    if (kind == 1) {
        SET(v, "name", yyjson_mut_false(d));
        SET(v, "version", yyjson_mut_uint(d, 42));
    }
    if (kind == 2) {
        yyjson_mut_obj_iter it = yyjson_mut_obj_iter_with(yyjson_mut_obj_get(v, "externals"));
        yyjson_mut_val *k = yyjson_mut_obj_iter_next(&it);
        SET(yyjson_mut_obj_iter_get_val(k), "mutable", yyjson_mut_uint(d, 42));
    }
    if (kind == 3) {
        SET(v, "mainHash", yyjson_mut_str(d, ZERO));
        SET(yyjson_mut_arr_get(yyjson_mut_obj_get(v, "native"), 0),
            "hash",
            yyjson_mut_str(d, ZERO));
    }
#undef SET
    m = pack(r, m);
    put(r, m.path, m.data, m.size);
    return m;
}

static void bad_zip_name(scenario_run_t *r, const char *root) {
    format_zip_entry_t e[3];
    format_zip_entry_init(e, "module.json", "{}", 2, false);
    format_zip_entry_init(e + 1, "bytecode.bin", "", 0, false);
    format_zip_entry_init(e + 2, "native/bad\xff.so", "opaque", 6, false);
    for (size_t i = 0; i < 3; i++)
        e[i].flags = 0;
    size_t n;
    char error[256];
    unsigned char *p = owned(r, format_zip_build(e, 3, NULL, 0, false, &n, error, sizeof(error)));
    put(r, root, p, n);
}

static yyjson_val *run(scenario_run_t *r,
                       configuration_t *c,
                       const char *root,
                       int status,
                       const char *mode,
                       const char *summary) {
    const char *args[] = {c->exe, mode, root, root, summary, NULL};
    const char *exe = c->exe;
#ifndef _WIN32
    const char *limited[] = {c->self, "--small-stack", c->exe, mode, root, root, summary, NULL};
    if (summary) {
        args[0] = c->exe;
        exe = c->self;
    }
    scenario_command_result_t out = language_command(r, exe, summary ? limited : args, NULL);
#else
    scenario_command_result_t out = language_command(r, exe, args, NULL);
#endif
    c->count++;
    REQUIRE(out.exit_code == 0);
    yyjson_val *v = parse(r, out.out, out.out_size);
    integer(r, v, "status", status);
    REQUIRE((yyjson_arr_size(get(v, "issues")) != 0) == (status == 2));
    return v;
}

static yyjson_val *node(scenario_run_t *r, yyjson_val *v, const char *name, const char *version) {
    size_t i, n;
    yyjson_val *x;
    yyjson_arr_foreach(get(v, "nodes"), i, n, x) {
        if (!strcmp(string(r, get(x, "name")), name)
            && !strcmp(string(r, get(x, "version")), version))
            return x;
    }
    REQUIRE(false);
    return NULL;
}

static int kind_count(yyjson_val *v, int kind) {
    int count = 0;
    size_t i, n;
    yyjson_val *x;
    yyjson_arr_foreach(get(v, "issues"), i, n, x) if (yyjson_get_int(get(x, "kind")) == kind)
        count++;
    return count;
}

static yyjson_val *chain_names(scenario_run_t *r, yyjson_val *v, yyjson_val *paths) {
    yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
    REQUIRE(d);
    scenario_defer(r, free_mut, d);
    yyjson_mut_val *all = yyjson_mut_arr(d);
    yyjson_mut_doc_set_root(d, all);
    size_t i, n;
    yyjson_val *p;
    yyjson_arr_foreach(paths, i, n, p) {
        yyjson_mut_val *chain = yyjson_mut_arr(d);
        size_t j, m;
        yyjson_val *edge;
        yyjson_arr_foreach(p, j, m, edge) {
            yyjson_val *e = yyjson_arr_get(get(v, "edges"), yyjson_get_uint(edge));
            yyjson_val *nd = yyjson_arr_get(get(v, "nodes"), yyjson_get_uint(get(e, "to")));
            REQUIRE(yyjson_mut_arr_append(chain, yyjson_val_mut_copy(d, get(nd, "name"))));
        }
        REQUIRE(yyjson_mut_arr_append(all, chain));
    }
    size_t size;
    char *s = owned(r, yyjson_mut_write(d, 0, &size));
    return parse(r, s, size);
}

static void chains(scenario_run_t *r, yyjson_val *v, yyjson_val *paths, const char *expected) {
    yyjson_val *a = chain_names(r, v, paths), *b = parse(r, expected, strlen(expected));
    REQUIRE(yyjson_arr_size(a) == yyjson_arr_size(b));
    size_t i, n;
    yyjson_val *x;
    yyjson_arr_foreach(b, i, n, x) {
        bool found = false;
        size_t j, m;
        yyjson_val *y;
        yyjson_arr_foreach(a, j, m, y) if (yyjson_equals(x, y)) found = true;
        REQUIRE(found);
    }
}

static void graph(scenario_run_t *r, void *argument) {
    configuration_t *c = argument;
    initialize(r, c);
    const char *project = path(r, "проект"), *root = join(r, project, "root.gbin");
    yyjson_val *v;
#define INSTALL(name, deps) install(r, c, project, name, "1", deps, NULL, false)
#define ROOT(deps) root_archive(r, c, root, deps, NULL, false)
#define RUN(status) run(r, c, root, status, "module", NULL)
#define I(k, x) integer(r, v, k, x)
#define HAS(k) REQUIRE(kind_count(v, k) > 0)
#define PATHS(name, s) chains(r, v, get(node(r, v, name, "1"), "paths"), s)
    const char *host_args[] = {c->exe, NULL};
    scenario_command_result_t host = language_command(r, c->exe, host_args, NULL);
    REQUIRE(host.exit_code == 0);
    ROOT(NULL);
    v = RUN(0);
    I("checks", 1);
    REQUIRE(yyjson_arr_size(get(v, "diagnostics")) == 0);
    REQUIRE(!strcmp(string(r, get(v, "diagnostic_text")), ""));
    module_t cm = INSTALL("c", NULL), b = INSTALL("b", list(r, cm.json, NULL));
    ROOT(list(r, b.json, NULL));
    v = RUN(0);
    I("node_count", 3);
    I("checks", 3);
    I("edge_count", 2);
    REQUIRE(yyjson_equals(get(get(node(r, v, "app", "1"), "metadata"), "dependencies"),
                          parse(r, list(r, b.json, NULL), strlen(list(r, b.json, NULL)))));
    REQUIRE(yyjson_equals(get(get(node(r, v, "b", "1"), "metadata"), "dependencies"),
                          parse(r, list(r, cm.json, NULL), strlen(list(r, cm.json, NULL)))));
    PATHS("c", "[[\"b\",\"c\"]]");
    module_t d = INSTALL("d", list(r, b.json, NULL));
    ROOT(list3(r, b.json, d.json, cm.json));
    v = RUN(0);
    I("node_count", 4);
    I("checks", 4);
    PATHS("c", "[[\"b\",\"c\"],[\"d\",\"b\",\"c\"],[\"c\"]]");
    REQUIRE(!kind_count(v, CYCLE));
    language_remove(r, cm.path);
    module_t neighbor = archive(r, c, "c", "1", NULL, NULL, false);
    put(r, join(r, project, "modules/b/modules/c/1.gbin"), neighbor.data, neighbor.size);
    v = RUN(2);
    REQUIRE(kind_count(v, SEARCH) == 1);
    integer(r, node(r, v, "c", "1"), "search_status", 3);
    PATHS("c", "[[\"b\",\"c\"],[\"d\",\"b\",\"c\"],[\"c\"]]");
    integer(r, node(r, v, "c", "1"), "attempts", 1);
    source(r, cm.path, "corrupt ZIP");
    v = RUN(2);
    REQUIRE(kind_count(v, SEARCH) == 1);
    I("checks", 4);
    integer(r, node(r, v, "c", "1"), "search_status", 4);
    module_t imposter = archive(r, c, "imposter", "1", NULL, NULL, false);
    put(r, cm.path, imposter.data, imposter.size);
    v = RUN(2);
    integer(r, node(r, v, "c", "1"), "search_status", 5);
    REQUIRE(kind_count(v, SEARCH) == 1);
    cm = INSTALL("c", NULL);
    module_t c2 = install(r, c, project, "c", "2", NULL, NULL, false);
    ROOT(list(r, b.json, c2.json));
    v = RUN(0);
    I("checks", 4);
    REQUIRE(!strcmp(string(r, get(node(r, v, "c", "2"), "hash")),
                    string(r, get(parse(r, c2.json, strlen(c2.json)), "hash"))));
    const char *wrong = "{\"name\":\"c\",\"version\":\"1\",\"hash\":\"" ZERO "\"}";
    d = INSTALL("d", list(r, wrong, NULL));
    ROOT(list(r, b.json, d.json));
    v = RUN(2);
    HAS(HASH);
    HAS(CONFLICT);
    yyjson_val *issue = NULL;
    size_t idx, max;
    yyjson_val *x;
    yyjson_arr_foreach(get(v, "issues"), idx, max, x) if (yyjson_get_int(get(x, "kind"))
                                                          == CONFLICT) {
        issue = x;
        break;
    }
    REQUIRE(issue);
    yyjson_val *ea = yyjson_arr_get(get(v, "edges"), yyjson_get_uint(get(issue, "edge"))),
               *eb = yyjson_arr_get(get(v, "edges"), yyjson_get_uint(get(issue, "other_edge")));
    const char *actual_hash = string(r, get(parse(r, cm.json, strlen(cm.json)), "hash"));
    const char *ha = string(r, get(ea, "hash")), *hb = string(r, get(eb, "hash"));
    REQUIRE((!strcmp(ha, actual_hash) && !strcmp(hb, ZERO))
            || (!strcmp(hb, actual_hash) && !strcmp(ha, ZERO)));
    chains(r,
           v,
           get(ea, "paths"),
           !strcmp(ha, actual_hash) ? "[[\"b\",\"c\"]]" : "[[\"d\",\"c\"]]");
    chains(r,
           v,
           get(eb, "paths"),
           !strcmp(hb, actual_hash) ? "[[\"b\",\"c\"]]" : "[[\"d\",\"c\"]]");
    module_t e = INSTALL("e", list(r, cm.json, NULL));
    ROOT(list3(r, b.json, d.json, e.json));
    v = RUN(2);
    PATHS("c", "[[\"b\",\"c\"],[\"d\",\"c\"],[\"e\",\"c\"]]");
    int correct = 0, incorrect = 0;
    yyjson_arr_foreach(get(v, "edges"), idx, max, x) {
        yyjson_val *to = yyjson_arr_get(get(v, "nodes"), yyjson_get_uint(get(x, "to")));
        if (!strcmp(string(r, get(to, "name")), "c")) {
            const char *h = string(r, get(x, "hash"));
            if (!strcmp(h, actual_hash))
                correct++;
            else if (!strcmp(h, ZERO))
                incorrect++;
            else
                REQUIRE(false);
        }
    }
    REQUIRE(correct == 2 && incorrect == 1);
    language_remove(r, cm.path);
    v = RUN(2);
    HAS(SEARCH);
    HAS(CONFLICT);
    cm = INSTALL("c", NULL);
    module_t bwrong = INSTALL("b", list(r, wrong, NULL));
    ROOT(list(r, bwrong.json, d.json));
    v = RUN(2);
    REQUIRE(kind_count(v, HASH) == 2 && !kind_count(v, CONFLICT));
    const char *missing = "{\"name\":\"missing\",\"version\":\"1\",\"hash\":\"" ONES "\"}";
    INSTALL("c", list(r, missing, NULL));
    ROOT(list(r, bwrong.json, NULL));
    v = RUN(2);
    HAS(HASH);
    HAS(SEARCH);
    const char *self = "{\"name\":\"app\",\"version\":\"1\",\"hash\":\"" ZERO "\"}";
    ROOT(list(r, self, NULL));
    v = RUN(2);
    I("checks", 1);
    I("node_count", 1);
    HAS(HASH);
    HAS(CYCLE);
    integer(r, yyjson_arr_get(get(v, "edges"), 0), "to", 0);
    REQUIRE(yyjson_equals(get(yyjson_arr_get(get(v, "edges"), 0), "paths"), parse(r, "[[0]]", 5)));
    b = INSTALL("b", list(r, self, NULL));
    ROOT(list(r, b.json, NULL));
    v = RUN(2);
    I("checks", 2);
    HAS(CYCLE);
    yyjson_arr_foreach(get(v, "issues"), idx, max, x) if (yyjson_get_int(get(x, "kind")) == CYCLE)
        chains(r,
               v,
               get(yyjson_arr_get(get(v, "edges"), yyjson_get_uint(get(x, "edge"))), "paths"),
               "[[\"b\",\"app\"]]");
    source(r, root, "broken root");
    v = RUN(2);
    I("node_count", 1);
    REQUIRE(yyjson_arr_size(get(v, "issues")) == 1 && kind_count(v, SEARCH) == 1);
    cm = INSTALL("c", NULL);
    const char *alias = "{\"alias\":{\"module\":\"c\",\"version\":\"1\",\"functionId\":1}}";
    b = install(r, c, project, "b", "1", list(r, cm.json, NULL), alias, false);
    root_archive(r, c, root, list(r, b.json, NULL), alias, false);
    v = RUN(0);
    const char *bj = list(r, b.json, NULL);
    REQUIRE(yyjson_equals(get(get(node(r, v, "app", "1"), "metadata"), "dependencies"),
                          parse(r, bj, strlen(bj))));
    b = install(r, c, project, "b", "1", NULL, alias, false);
    ROOT(list(r, b.json, cm.json));
    v = RUN(2);
    REQUIRE(yyjson_arr_size(get(v, "issues")) == 1 && kind_count(v, REFERENCE) == 1);
    REQUIRE(!strcmp(
        string(
            r,
            get(yyjson_arr_get(get(v, "nodes"),
                               yyjson_get_uint(get(yyjson_arr_get(get(v, "issues"), 0), "node"))),
                "name")),
        "b"));
    const char *badrefs = "{\"badId\":{\"module\":\"c\",\"version\":\"1\",\"functionId\":7},"
                          "\"badVersion\":{\"module\":\"c\",\"version\":\"2\",\"functionId\":1}}";
    b = install(r, c, project, "b", "1", list(r, cm.json, NULL), badrefs, false);
    ROOT(list(r, b.json, c2.json));
    v = RUN(2);
    REQUIRE(kind_count(v, REFERENCE) == 2);
    bool id = false, ver = false;
    yyjson_arr_foreach(get(v, "issues"), idx, max, x) {
        const char *s = string(r, get(x, "reference"));
        id |= !strcmp(s, "badId");
        ver |= !strcmp(s, "badVersion");
    }
    REQUIRE(id && ver);
    module_t broken = INSTALL("broken", NULL);
    source(r, broken.path, "corrupt");
    ROOT(list3(r, b.json, broken.json, missing));
    v = RUN(2);
    REQUIRE(kind_count(v, SEARCH) == 2 && kind_count(v, REFERENCE) == 2);
    const char *blocked = "{\"badId\":{\"module\":\"c\",\"version\":\"1\",\"functionId\":7},"
                          "\"blocked\":{\"module\":\"unseen\",\"version\":\"1\",\"functionId\":1}}";
    b = install(r, c, project, "b", "1", list(r, cm.json, missing), blocked, false);
    ROOT(list(r, b.json, NULL));
    v = RUN(2);
    HAS(SEARCH);
    HAS(REFERENCE);
    integer(r, node(r, v, "b", "1"), "references_skipped", 1);
    module_t opaque = install(r, c, project, "opaque", "1", NULL, NULL, true);
    ROOT(list(r, opaque.json, NULL));
    v = RUN(0);
    I("checks", 2);
    const char *desc = join(r, project, "app.gmod"), *main = join(r, project, "main.goat");
    source(r,
           desc,
           "{\"formatVersion\":1,\"name\":\"source\",\"version\":\"1\",\"main\":\"main.goat\","
           "\"dependencies\":{\"b\":\"1\",\"unused\":\"not-installed\"}}");
    b = INSTALL("b", list(r, cm.json, NULL));
    source(r, main, "include \"own.goat\"; if(false){import \"b\";}");
    source(r, join(r, project, "own.goat"), "func(){import \"c@2\";}");
    v = run(r, c, desc, 0, "gmod", NULL);
    I("node_count", 4);
    I("checks", 3);
    int from = 0;
    yyjson_arr_foreach(get(v, "edges"), idx, max, x) if (yyjson_get_int(get(x, "from")) == 0)
        from++;
    REQUIRE(from == 2);
    language_remove(r, cm.path);
    v = run(r, c, desc, 2, "gmod", NULL);
    integer(r, node(r, v, "c", "1"), "search_status", 3);
    source(r, main, "import \"c\"; import \"c@2\";");
    run(r, c, desc, 105, "gmod", NULL);
    source(r, main, "");
    v = run(r, c, main, 0, "goat", NULL);
    I("checks", 0);
    I("node_count", 1);
    module_t old = INSTALL("app", NULL);
    b = INSTALL("b", list(r, old.json, NULL));
    source(r, main, "import \"b@1\";");
    v = run(r, c, main, 0, "goat", NULL);
    I("checks", 2);
    ROOT(list(r, b.json, NULL));
    v = RUN(2);
    HAS(CYCLE);
    HAS(HASH);
    I("checks", 2);
#undef INSTALL
#undef ROOT
#undef RUN
#undef I
#undef HAS
#undef PATHS
}

/* Diagnostic equivalence ignores discovery indices, retaining full semantic chains. */
static yyjson_val *diagnostic_node(yyjson_val *result, yyjson_val *row, size_t position) {
    if (!position)
        return yyjson_arr_get(get(result, "nodes"), 0);
    yyjson_val *edge =
        yyjson_arr_get(get(result, "edges"),
                       yyjson_get_uint(yyjson_arr_get(get(row, "path"), position - 1)));
    return yyjson_arr_get(get(result, "nodes"), yyjson_get_uint(get(edge, "to")));
}

static int chain_compare(scenario_run_t *r, yyjson_val *result, yyjson_val *a, yyjson_val *b) {
    size_t an = yyjson_arr_size(get(a, "path")) + 1, bn = yyjson_arr_size(get(b, "path")) + 1;
    for (size_t i = 0; i < an && i < bn; i++) {
        yyjson_val *av = diagnostic_node(result, a, i), *bv = diagnostic_node(result, b, i);
        int n = strcmp(string(r, get(av, "name")), string(r, get(bv, "name")));
        if (!n)
            n = strcmp(string(r, get(av, "version")), string(r, get(bv, "version")));
        if (n)
            return n;
    }
    return an < bn ? -1 : an > bn;
}

static void diagnostic_order(scenario_run_t *r, yyjson_val *result, bool kinds) {
    yyjson_val *rows = get(result, "diagnostics");
    for (size_t i = 1; i < yyjson_arr_size(rows); i++) {
        yyjson_val *a = yyjson_arr_get(rows, i - 1), *b = yyjson_arr_get(rows, i);
        int order = chain_compare(r, result, a, b);
        REQUIRE(order <= 0);
        if (!order && kinds)
            REQUIRE(yyjson_get_uint(get(a, "kind")) <= yyjson_get_uint(get(b, "kind")));
    }
}

static const char *diagnostic_rows(scenario_run_t *r, yyjson_val *result) {
    yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
    REQUIRE(d != NULL);
    scenario_defer(r, free_mut, d);
    yyjson_mut_val *out = yyjson_mut_arr(d);
    yyjson_mut_doc_set_root(d, out);
    yyjson_val *rows = get(result, "diagnostics");
    for (size_t i = 0; i < yyjson_arr_size(rows); i++) {
        yyjson_val *row = yyjson_arr_get(rows, i), *p = get(row, "path");
        yyjson_mut_val *entry = yyjson_mut_arr(d), *chain = yyjson_mut_arr(d);
        yyjson_mut_arr_append(out, entry);
        yyjson_mut_arr_append(entry, chain);
        for (size_t j = 0; j <= yyjson_arr_size(p); j++) {
            yyjson_val *n = diagnostic_node(result, row, j);
            yyjson_mut_val *pair = yyjson_mut_arr(d);
            yyjson_mut_arr_append(chain, pair);
            yyjson_mut_arr_append(pair, yyjson_val_mut_copy(d, get(n, "name")));
            yyjson_mut_arr_append(pair, yyjson_val_mut_copy(d, get(n, "version")));
        }
        yyjson_mut_arr_append(entry, yyjson_val_mut_copy(d, get(row, "kind")));
        yyjson_mut_arr_append(entry, yyjson_val_mut_copy(d, get(row, "reference")));
        yyjson_val *hash = NULL;
        if (yyjson_arr_size(p)) {
            yyjson_val *edge =
                yyjson_arr_get(get(result, "edges"),
                               yyjson_get_uint(yyjson_arr_get(p, yyjson_arr_size(p) - 1)));
            hash = get(edge, "hash");
        }
        yyjson_mut_arr_append(entry, hash ? yyjson_val_mut_copy(d, hash) : yyjson_mut_str(d, ""));
        yyjson_val *v = get(row, "validation");
        yyjson_mut_arr_append(entry, v ? yyjson_val_mut_copy(d, v) : yyjson_mut_null(d));
    }
    return owned(r, yyjson_mut_write(d, 0, NULL));
}

static bool chain_is(scenario_run_t *r,
                     yyjson_val *result,
                     yyjson_val *row,
                     const char *const *names,
                     size_t count) {
    if (yyjson_arr_size(get(row, "path")) + 1 != count)
        return false;
    for (size_t i = 0; i < count; i++)
        if (strcmp(string(r, get(diagnostic_node(result, row, i), "name")), names[i]))
            return false;
    return true;
}

static void same_diagnostics(scenario_run_t *r, yyjson_val *a, yyjson_val *b) {
    REQUIRE(!strcmp(diagnostic_rows(r, a), diagnostic_rows(r, b)));
    REQUIRE(!strcmp(string(r, get(a, "diagnostic_text")), string(r, get(b, "diagnostic_text"))));
}

static void diagnostics(scenario_run_t *r, void *argument) {
    configuration_t *c = argument;
    initialize(r, c);
    const char *project = path(r, "проект"), *root = join(r, project, "root.gbin");
    const char *zero = "0000000000000000000000000000000000000000000000000000000000000000";
    const char *ones = "1111111111111111111111111111111111111111111111111111111111111111";
    const char *wrong_shared =
        "{\"name\":\"shared\",\"version\":\"1\",\"hash\":"
        "\"0000000000000000000000000000000000000000000000000000000000000000\"}";
    const char *other_shared =
        "{\"name\":\"shared\",\"version\":\"1\",\"hash\":"
        "\"1111111111111111111111111111111111111111111111111111111111111111\"}";
    const char *missing = "{\"name\":\"missing\",\"version\":\"1\",\"hash\":"
                          "\"1111111111111111111111111111111111111111111111111111111111111111\"}";
    module_t shared = install(r, c, project, "shared", "1", NULL, NULL, false);
    module_t first = install(r, c, project, "first", "1", list(r, shared.json, NULL), NULL, false);
    install(r, c, project, "second", "1", list(r, wrong_shared, NULL), NULL, false);
    install(r, c, project, "third", "1", list(r, shared.json, NULL), NULL, false);
    install(r, c, project, "fourth", "1", list(r, other_shared, NULL), NULL, false);
    const char *src = join(r, project, "diagnostics.goat");
    source(r,
           src,
           "import \"third@1\"; import \"second@1\"; import \"first@1\"; import \"fourth@1\";");
    yyjson_val *result = run(r, c, src, 2, "goat", NULL);
    yyjson_val *rows = get(result, "diagnostics");
    size_t conflicts = 0;
    unsigned parents = 0, hashes = 0;
    yyjson_val *record = parse(r, shared.json, strlen(shared.json));
    const char *actual = string(r, get(record, "hash"));
    for (size_t i = 0; i < yyjson_arr_size(rows); i++) {
        yyjson_val *row = yyjson_arr_get(rows, i);
        if (yyjson_get_uint(get(row, "kind")) != 2)
            continue;
        conflicts++;
        size_t n = yyjson_arr_size(get(row, "path"));
        const char *name = string(r, get(diagnostic_node(result, row, n - 1), "name"));
        const char *names[] = {"first", "second", "third", "fourth"};
        for (size_t j = 0; j < N(names); j++)
            if (!strcmp(name, names[j]))
                parents |= 1u << j;
        yyjson_val *edge = yyjson_arr_get(get(result, "edges"),
                                          yyjson_get_uint(yyjson_arr_get(get(row, "path"), n - 1)));
        const char *hash = string(r, get(edge, "hash"));
        REQUIRE(!strcmp(hash, actual) || !strcmp(hash, zero) || !strcmp(hash, ones));
        if (!strcmp(hash, actual))
            hashes |= 1;
        if (!strcmp(hash, zero))
            hashes |= 2;
        if (!strcmp(hash, ones))
            hashes |= 4;
    }
    REQUIRE(conflicts == 4 && parents == 15 && hashes == 7);
    diagnostic_order(r, result, false);
    const char *text = string(r, get(result, "diagnostic_text"));
    REQUIRE(strstr(text, actual) && strstr(text, zero) && strstr(text, ones));
    source(r,
           src,
           "import \"fourth@1\"; import \"first@1\"; import \"second@1\"; import \"third@1\";");
    same_diagnostics(r, result, run(r, c, src, 2, "goat", NULL));
    install(r, c, project, "late", "1", list(r, first.json, NULL), NULL, false);
    source(r,
           src,
           "import \"late@1\"; import \"second@1\"; import \"first@1\"; import \"third@1\";");
    result = run(r, c, src, 2, "goat", NULL);
    rows = get(result, "diagnostics");
    conflicts = 0;
    bool late = false;
    const char *late_names[] = {"<source>", "late", "first", "shared"};
    for (size_t i = 0; i < yyjson_arr_size(rows); i++) {
        yyjson_val *row = yyjson_arr_get(rows, i);
        if (yyjson_get_uint(get(row, "kind")) == 2) {
            conflicts++;
            late |= chain_is(r, result, row, late_names, N(late_names));
        }
    }
    REQUIRE(conflicts == 4 && late);
    shared = install(r, c, project, "shared", "1", NULL, NULL, false);
    rewrite(r, c, shared, 1);
    result = run(r, c, src, 2, "goat", NULL);
    rows = get(result, "diagnostics");
    size_t fields = 0, chains = 0;
    yyjson_val *previous = NULL;
    bool skipped = false;
    for (size_t i = 0; i < yyjson_arr_size(rows); i++) {
        yyjson_val *row = yyjson_arr_get(rows, i), *v = get(row, "validation");
        if (!v)
            continue;
        skipped |= yyjson_get_uint(get(v, "state")) == 2;
        const char *field = string(r, get(v, "field"));
        if (yyjson_get_uint(get(v, "state")) != 1
            || (strcmp(field, "/name") && strcmp(field, "/version")))
            continue;
        REQUIRE(!strcmp(string(r, get(v, "entry")), "module.json"));
        if (!previous || chain_compare(r, result, previous, row)) {
            REQUIRE(fields % 2 == 0);
            chains++;
        }
        REQUIRE(!strcmp(field, fields % 2 ? "/version" : "/name"));
        previous = row;
        fields++;
    }
    REQUIRE(fields == 8 && chains == 4 && skipped);
    integer(r, result, "checks", 5);
    diagnostic_order(r, result, false);
    text = string(r, get(result, "diagnostic_text"));
    REQUIRE(strstr(text, "/name") && strstr(text, "/version"));
    same_diagnostics(r, result, run(r, c, src, 2, "goat", NULL));
    shared = install(r, c, project, "shared", "1", NULL, NULL, false);
    const char *refs = "{\"zebra\":{\"module\":\"shared\",\"version\":\"1\",\"functionId\":8},"
                       "\"alpha\":{\"module\":\"shared\",\"version\":\"1\",\"functionId\":9}}";
    const char *reversed = "{\"alpha\":{\"module\":\"shared\",\"version\":\"1\",\"functionId\":9},"
                           "\"zebra\":{\"module\":\"shared\",\"version\":\"1\",\"functionId\":8}}";
    first = install(r, c, project, "first", "1", list(r, shared.json, NULL), refs, false);
    root_archive(r, c, root, list(r, first.json, missing), NULL, false);
    result = run(r, c, root, 2, "module", NULL);
    rows = get(result, "diagnostics");
    size_t references = 0;
    for (size_t i = 0; i < yyjson_arr_size(rows); i++) {
        yyjson_val *row = yyjson_arr_get(rows, i);
        if (yyjson_get_uint(get(row, "kind")) != 4)
            continue;
        REQUIRE(references < 2);
        REQUIRE(!strcmp(string(r, get(row, "reference")), references ? "zebra" : "alpha"));
        references++;
    }
    REQUIRE(references == 2);
    diagnostic_order(r, result, true);
    module_t first2 =
        install(r, c, project, "first", "1", list(r, shared.json, NULL), reversed, false);
    REQUIRE(!strcmp(first.json, first2.json));
    same_diagnostics(r, result, run(r, c, root, 2, "module", NULL));
    const char *nul_refs =
        "{\"alpha\\u0000z\":{\"module\":\"shared\",\"version\":\"1\",\"functionId\":8},"
        "\"alpha\\u0000a\":{\"module\":\"shared\",\"version\":\"1\",\"functionId\":9}}";
    first = install(r, c, project, "first", "1", list(r, shared.json, NULL), nul_refs, false);
    root_archive(r, c, root, list(r, first.json, NULL), NULL, false);
    result = run(r, c, root, 2, "module", NULL);
    rows = get(result, "diagnostics");
    references = 0;
    for (size_t i = 0; i < yyjson_arr_size(rows); i++) {
        yyjson_val *row = yyjson_arr_get(rows, i);
        integer(r, row, "reference_length", 7);
        if (yyjson_get_uint(get(row, "kind")) != 4)
            continue;
        yyjson_val *name = get(row, "reference");
        REQUIRE(references < 2 && yyjson_get_len(name) == 7);
        REQUIRE(!memcmp(string(r, name), references ? "alpha\0z" : "alpha\0a", 7));
        references++;
    }
    REQUIRE(references == 2);
    text = string(r, get(result, "diagnostic_text"));
    REQUIRE(strstr(text, "alpha\\x00a") && strstr(text, "alpha\\x00z"));
    const char *app = "{\"name\":\"app\",\"version\":\"1\",\"hash\":"
                      "\"0000000000000000000000000000000000000000000000000000000000000000\"}";
    first = install(r, c, project, "first", "1", list(r, app, NULL), NULL, false);
    root_archive(r, c, root, list(r, first.json, NULL), NULL, false);
    result = run(r, c, root, 2, "module", NULL);
    rows = get(result, "diagnostics");
    size_t cycles = 0;
    const char *cycle_names[] = {"app", "first", "app"};
    for (size_t i = 0; i < yyjson_arr_size(rows); i++) {
        yyjson_val *row = yyjson_arr_get(rows, i);
        if (yyjson_get_uint(get(row, "kind")) != 3)
            continue;
        cycles++;
        REQUIRE(chain_is(r, result, row, cycle_names, N(cycle_names)));
    }
    REQUIRE(cycles == 1);
    same_diagnostics(r, result, run(r, c, root, 2, "module", NULL));
    const char *ca = "{\"name\":\"cyclea\",\"version\":\"1\",\"hash\":"
                     "\"1111111111111111111111111111111111111111111111111111111111111111\"}";
    const char *cb = "{\"name\":\"cycleb\",\"version\":\"1\",\"hash\":"
                     "\"0000000000000000000000000000000000000000000000000000000000000000\"}";
    module_t cyclea = install(r, c, project, "cyclea", "1", list(r, cb, NULL), NULL, false);
    module_t cycleb = install(r, c, project, "cycleb", "1", list(r, ca, NULL), NULL, false);
    root_archive(r, c, root, list(r, cycleb.json, cyclea.json), NULL, false);
    result = run(r, c, root, 2, "module", NULL);
    rows = get(result, "diagnostics");
    cycles = 0;
    unsigned witnesses = 0;
    const char *an[] = {"app", "cyclea", "cycleb", "cyclea"},
               *bn[] = {"app", "cycleb", "cyclea", "cycleb"};
    for (size_t i = 0; i < yyjson_arr_size(rows); i++) {
        yyjson_val *row = yyjson_arr_get(rows, i);
        if (yyjson_get_uint(get(row, "kind")) != 3)
            continue;
        cycles++;
        bool a = chain_is(r, result, row, an, N(an)), b = chain_is(r, result, row, bn, N(bn));
        REQUIRE(a || b);
        if (a)
            witnesses |= 1;
        if (b)
            witnesses |= 2;
    }
    REQUIRE(cycles == 2 && witnesses == 3);
    source(r, root, "bad ZIP");
    yyjson_val *observed = run(r, c, root, 2, "module", NULL),
               *unobserved = run(r, c, root, 2, "module-null", NULL);
    integer(r, unobserved, "checks", 0);
    REQUIRE(yyjson_get_uint(get(yyjson_arr_get(get(unobserved, "nodes"), 0), "validation_count"))
            > 0);
    same_diagnostics(r, observed, unobserved);
    bad_zip_name(r, root);
    result = run(r, c, root, 2, "module", NULL);
    rows = get(result, "diagnostics");
    bool found = false;
    const unsigned char bad_name[] = "native/bad\xff.so";
    for (size_t i = 0; i < yyjson_arr_size(rows); i++) {
        yyjson_val *v = get(yyjson_arr_get(rows, i), "validation");
        if (!v)
            continue;
        yyjson_val *bytes = get(v, "entry_bytes");
        if (yyjson_arr_size(bytes) != sizeof(bad_name) - 1)
            continue;
        bool equal = true;
        for (size_t j = 0; j < sizeof(bad_name) - 1; j++)
            equal &= yyjson_get_uint(yyjson_arr_get(bytes, j)) == bad_name[j];
        found |= equal;
    }
    REQUIRE(found);
    REQUIRE(strstr(string(r, get(result, "diagnostic_text")), "native/bad\\xff.so"));
    refs = "{\"line\\n\\\\suffix\":{\"module\":\"absent\",\"version\":\"1\",\"functionId\":1}}";
    module_t damaged = root_archive(r, c, root, NULL, refs, false);
    rewrite(r, c, damaged, 2);
    result = run(r, c, root, 2, "module", NULL);
    rows = get(result, "diagnostics");
    found = false;
    for (size_t i = 0; i < yyjson_arr_size(rows); i++) {
        yyjson_val *v = get(yyjson_arr_get(rows, i), "validation");
        if (v && strstr(string(r, get(v, "field")), "line\n\\suffix"))
            found = true;
    }
    REQUIRE(found);
    REQUIRE(strstr(string(r, get(result, "diagnostic_text")), "line\\x0a\\\\suffix"));
    refs = "{\"café\":{\"module\":\"absent\",\"version\":\"1\",\"functionId\":1},"
           "\"x\\u0085z\":{\"module\":\"absent\",\"version\":\"1\",\"functionId\":1},"
           "\"x\\u202ez\":{\"module\":\"absent\",\"version\":\"1\",\"functionId\":1},"
           "\"x\\u2069z\":{\"module\":\"absent\",\"version\":\"1\",\"functionId\":1}}";
    root_archive(r, c, root, NULL, refs, false);
    result = run(r, c, root, 2, "module", NULL);
    text = string(r, get(result, "diagnostic_text"));
    REQUIRE(strstr(text, "café") && strstr(text, "x\\u0085z") && strstr(text, "x\\u202ez")
            && strstr(text, "x\\u2069z"));
#ifndef _WIN32
    const char *label = join(r, project, "line\n\\label.gbin");
    source(r, label, "bad ZIP");
    result = run(r, c, label, 2, "module", NULL);
    REQUIRE(strstr(string(r, get(result, "diagnostic_text")), "line\\x0a\\\\label.gbin"));
#endif
    damaged = root_archive(r, c, root, NULL, NULL, true);
    rewrite(r, c, damaged, 3);
    result = run(r, c, root, 2, "module", NULL);
    rows = get(result, "diagnostics");
    size_t errors = 0;
    for (size_t i = 0; i < yyjson_arr_size(rows); i++) {
        yyjson_val *v = get(yyjson_arr_get(rows, i), "validation");
        if (!v)
            continue;
        uint64_t status = yyjson_get_uint(get(v, "container_status"));
        if (status != 12 && status != 13)
            continue;
        REQUIRE(errors < 2 && status == (errors ? 12 : 13));
        REQUIRE(!strcmp(string(r, get(v, "entry")), errors ? "module.json" : "native/test.so"));
        errors++;
    }
    REQUIRE(errors == 2);
}

static void deep(scenario_run_t *r, void *argument) {
    configuration_t *c = argument;
    const char *project = path(r, "проект"), *root = join(r, project, "root.gbin");
    const char *dep = NULL;
    char name[64];
    for (int i = 2499; i >= 0; i--) {
        snprintf(name, sizeof(name), "deep%d", i);
        dep = install(r, c, project, name, "1", dep ? list(r, dep, NULL) : NULL, NULL, false).json;
    }
    root_archive(r, c, root, list(r, dep, NULL), NULL, false);
    yyjson_val *v = run(r, c, root, 0, "module", "summary");
    integer(r, v, "node_count", 2501);
    integer(r, v, "checks", 2501);
    integer(r, v, "last_path_length", 2500);
    dep = "{\"name\":\"deepmissing\",\"version\":\"1\",\"hash\":\"" ZERO "\"}";
    for (int i = 2499; i >= 0; i--) {
        snprintf(name, sizeof(name), "deep%d", i);
        dep = install(r, c, project, name, "1", list(r, dep, NULL), NULL, false).json;
    }
    root_archive(r, c, root, list(r, dep, NULL), NULL, false);
    v = run(r, c, root, 2, "module", "diagnostics");
    integer(r, v, "node_count", 2502);
    integer(r, v, "checks", 2501);
    REQUIRE(yyjson_arr_size(get(v, "diagnostics")) == 1);
    REQUIRE(yyjson_arr_size(get(yyjson_arr_get(get(v, "diagnostics"), 0), "path")) == 2501);
    REQUIRE(strstr(string(r, get(v, "diagnostic_text")), "deepmissing"));
    const char *deps = list(r, install(r, c, project, "bottom", "1", NULL, NULL, false).json, NULL);
    module_t a, b;
    for (int i = 31; i >= 0; i--) {
        snprintf(name, sizeof(name), "layer%da", i);
        a = install(r, c, project, name, "1", deps, NULL, false);
        snprintf(name, sizeof(name), "layer%db", i);
        b = install(r, c, project, name, "1", deps, NULL, false);
        deps = list(r, a.json, b.json);
    }
    module_t z = install(r, c, project, "zisolated", "1", NULL, NULL, false);
    root_archive(r, c, root, list3(r, a.json, b.json, z.json), NULL, false);
    v = run(r, c, root, 0, "module", "summary");
    integer(r, v, "node_count", 67);
    integer(r, v, "checks", 67);
    integer(r, v, "last_path_length", 33);
}

static void body(scenario_run_t *r, void *argument) {
    configuration_t *c = argument;
    graph(r, c);
    diagnostics(r, c);
    deep(r, c);
#ifdef _WIN32
    REQUIRE(c->count == 47);
#else
    REQUIRE(c->count == 48);
#endif
    printf("%zu module graph fixtures passed\n", c->count);
}

int main(int argc, char **argv) {
#ifndef _WIN32
    if (argc >= 4 && !strcmp(argv[1], "--small-stack")) {
        struct rlimit limit;
        if (getrlimit(RLIMIT_STACK, &limit))
            return 125;
        limit.rlim_cur = 256 * 1024;
        if (setrlimit(RLIMIT_STACK, &limit))
            return 125;
        execv(argv[2], argv + 2);
        return 125;
    }
#else
    fs_error_t arg_error = {0};
    if (!fs_arguments(&argc, &argv, &arg_error)) {
        clear_fs_error(&arg_error);
        return 1;
    }
#endif
    int result = 1;
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        puts("integration.module_graph");
        result = 0;
    } else if (argc == 6 || argc == 7) {
        fs_error_t e = {0};
        char *exe = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        char *vectors = absolute_fs_path(argv[5], &e);
        clear_fs_error(&e);
        char *self = absolute_fs_path(argv[0], &e);
        clear_fs_error(&e);
        if (exe && vectors && self) {
            configuration_t c = {.exe = exe, .vectors = vectors, .self = self};
            scenario_definition_t def = {"integration.module_graph", 600000, body, &c, NULL};
            scenario_options_t o = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(&def, 1, argc == 7 ? 2 : 1, selection, &o);
        }
        FREE(exe);
        FREE(vectors);
        FREE(self);
    } else
        fputs("usage: module_graph_scenarios ID HOST BUILD DATA VECTORS [--keep-work]\n", stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
