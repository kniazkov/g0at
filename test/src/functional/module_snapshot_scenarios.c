/** @file module_snapshot_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Pinned immutable module snapshots and synchronized mutation fixtures (#353).
 */
#ifndef _WIN32
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#ifndef _WIN32
#    include <sys/resource.h>
#    include <unistd.h>
#endif
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/module_snapshot_fixture.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>
#define N(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    const char *exe, *vectors, *self;
    yyjson_mut_doc *schema;
} configuration_t;

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

static const char *path(scenario_run_t *r, const char *s) {
    return language_join(r, scenario_work_directory(scenario_run_context(r)), s);
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

#define REQUIRE(x) scenario_require(r, (x), #x)
#define ZERO "0000000000000000000000000000000000000000000000000000000000000000"

#define OLD "старый 😀"

typedef snapshot_fixture_t module_t;

static const char *list(scenario_run_t *r, const char *a, const char *b) {
    char *s = owned(r, malloc((a ? strlen(a) : 0) + (b ? strlen(b) : 0) + 4));
    sprintf(s, "[%s%s%s]", a ? a : "", a && b ? "," : "", b ? b : "");
    return s;
}

static void integer(scenario_run_t *r, yyjson_val *v, const char *k, int x) {
    REQUIRE(yyjson_is_int(get(v, k)) && yyjson_get_int(get(v, k)) == x);
}

static module_t install(scenario_run_t *r, const char *p, module_t m) {
    m.path = put(r, p, m.data, m.size);
    return m;
}

static yyjson_val *node(scenario_run_t *r, yyjson_val *v, const char *name, const char *version) {
    size_t j, n;
    yyjson_val *x;
    yyjson_arr_foreach(get(v, "nodes"), j, n, x) if (!strcmp(string(r, get(x, "name")), name)
                                                     && !strcmp(string(r, get(x, "version")),
                                                                version)) return x;
    REQUIRE(false);
    return NULL;
}

static void values(scenario_run_t *r,
                   yyjson_val *v,
                   const char *name,
                   const char *version,
                   int first,
                   size_t count) {
    yyjson_val *a = get(node(r, v, name, version), "values");
    REQUIRE(yyjson_arr_size(a) == count);
    for (size_t j = 0; j < count; j++)
        REQUIRE(yyjson_is_int(yyjson_arr_get(a, j))
                && yyjson_get_int(yyjson_arr_get(a, j)) == first + (int)j);
}

static yyjson_val *run(scenario_run_t *r,
                       configuration_t *c,
                       const char *root,
                       int status,
                       const char *mode,
                       scenario_body_t action,
                       void *user,
                       bool small) {
    const char *args[] = {c->exe, root, action ? "sync" : mode, NULL};
    const char *exe = c->exe;
    const char *const *argv = args;
#ifndef _WIN32
    const char *limited[] = {c->self, "--small-stack", c->exe, root, mode, NULL};
    if (small) {
        exe = c->self;
        argv = limited;
    }
#else
    (void)small;
#endif
    child_options_t opts = {.executable = exe,
                            .arguments = argv,
                            .environment = language_environment(r, false)};
    scenario_command_result_t out =
        action ? scenario_command_barrier(r, &opts, "READY\n", action, user)
               : scenario_command_capture(r, &opts);
    REQUIRE(out.exit_code == 0);
    const unsigned char *text = out.out;
    size_t n = out.out_size;
    if (!status) {
        size_t prefix = n >= 7 && !memcmp(text, "READY\r\n", 7) ? 7 : 6;
        REQUIRE(n >= prefix);
        scenario_require_bytes(r, "READY", "READY\n", 6, text, prefix, true);
        text += prefix;
        n -= prefix;
    }
    yyjson_val *v = parse(r, text, n);
    integer(r, v, "status", status);
    if (!status)
        integer(r, v, "checks", (int)yyjson_arr_size(get(v, "nodes")));
    else {
        REQUIRE(!get(v, "nodes"));
        if (strcmp(mode, "no-diagnostics"))
            REQUIRE(yyjson_get_int(get(v, "issues")) > 0);
    }
    return v;
}

typedef struct {
    const char *root, *bp, *cp;
    module_t root_new, b_new, c_new;
    int kind;
} mutation_t;

static void mutate_files(scenario_run_t *r, void *arg) {
    mutation_t *m = arg;
    const char *paths[] = {m->root, m->bp, m->cp};
    module_t mods[] = {m->root_new, m->b_new, m->c_new};
    for (size_t j = 0; j < 3; j++)
        if (paths[j]) {
            if (m->kind == 0)
                put(r, paths[j], mods[j].data, mods[j].size);
            else if (m->kind == 1)
                language_remove(r, paths[j]);
            else
                put(r, paths[j], "not a zip", 9);
        }
}

static void remove_tree(scenario_run_t *r, void *arg) {
    fs_error_t e = {0};
    bool ok = remove_fs_tree(arg, &e);
    clear_fs_error(&e);
    REQUIRE(ok);
}

#define ARCH(name, version, deps, value, label, files, throwing, native, dormant)                  \
    snapshot_fixture_archive(r,                                                                    \
                             c->vectors,                                                           \
                             &c->schema,                                                           \
                             name,                                                                 \
                             version,                                                              \
                             deps,                                                                 \
                             value,                                                                \
                             label,                                                                \
                             files,                                                                \
                             throwing,                                                             \
                             native,                                                               \
                             dormant)
#define MOD(name, deps, value) ARCH(name, "1", deps, value, OLD, 1, false, false, NULL)
#define ROOT(deps, value, files, throwing, native)                                                 \
    install(r, root, ARCH("app", "1", deps, value, OLD, files, throwing, native, NULL))
#define RUN(status) run(r, c, root, status, "run", NULL, NULL, false)

static void files(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *root = path(r, "root.gbin");
    const char *ha[] = {c->exe, NULL};
    REQUIRE(language_command(r, c->exe, ha, NULL).exit_code == 0);
    unsigned counts[] = {1, 3, 11};
    for (size_t j = 0; j < N(counts); j++) {
        ROOT(NULL, 100, counts[j], false, false);
        yyjson_val *v = RUN(0);
        values(r, v, "app", "1", 100, counts[j]);
        REQUIRE(!strcmp(string(r, get(node(r, v, "app", "1"), "label")), OLD));
    }
}

static void mutation(scenario_run_t *r, void *arg, int kind) {
    configuration_t *c = arg;
    const char *root = path(r, "root.gbin"), *bp = path(r, "modules/b/1.gbin"),
               *cp = path(r, "modules/c/1.gbin");
    module_t cm = install(r, cp, ARCH("c", "1", NULL, 30, OLD, 1, false, true, NULL));
    module_t b =
        install(r, bp, ARCH("b", "1", list(r, cm.record, NULL), 20, OLD, 1, false, false, "c@1"));
    ROOT(list(r, b.record, NULL), 10, 3, false, true);
    mutation_t m = {.root = root, .bp = bp, .cp = cp, .kind = kind};
    if (kind == 0) {
        m.c_new = ARCH("c", "1", NULL, 300, "new", 1, false, true, NULL);
        m.b_new = ARCH("b", "1", list(r, m.c_new.record, NULL), 200, "new", 1, false, false, NULL);
        m.root_new =
            ARCH("app", "1", list(r, m.b_new.record, NULL), 1000, "new", 3, false, true, NULL);
    }
    yyjson_val *v = run(r, c, root, 0, "run", mutate_files, &m, false);
    values(r, v, "app", "1", 10, 3);
    values(r, v, "b", "1", 20, 1);
    values(r, v, "c", "1", 30, 1);
    char hash[65];
    const char *lib = "never OS-load: " OLD;
    format_binary_sha256(lib, strlen(lib), hash);
    const char *names[] = {"app", "c"};
    for (size_t j = 0; j < 2; j++) {
        yyjson_val *a = get(node(r, v, names[j], "1"), "native");
        REQUIRE(yyjson_arr_size(a) == 1);
        REQUIRE(!strcmp(string(r, yyjson_arr_get(a, 0)), hash));
    }
    if (!kind) {
        v = RUN(0);
        values(r, v, "app", "1", 1000, 3);
        values(r, v, "b", "1", 200, 1);
        values(r, v, "c", "1", 300, 1);
        yyjson_val *a = get(node(r, v, "c", "1"), "native");
        REQUIRE(yyjson_arr_size(a) == 1);
        REQUIRE(strcmp(string(r, yyjson_arr_get(a, 0)), hash));
    } else {
        if (kind == 1)
            ROOT(list(r, b.record, NULL), 100, 1, false, false);
        RUN(2);
    }
}

static void replace_case(scenario_run_t *r, void *a) {
    mutation(r, a, 0);
}

static void delete_case(scenario_run_t *r, void *a) {
    mutation(r, a, 1);
}

static void corrupt_case(scenario_run_t *r, void *a) {
    mutation(r, a, 2);
}

static void diamond(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *root = path(r, "root.gbin");
    module_t cm = install(r, path(r, "modules/c/1.gbin"), MOD("c", NULL, 3));
    module_t b = install(r, path(r, "modules/b/1.gbin"), MOD("b", list(r, cm.record, NULL), 2));
    module_t d = install(r, path(r, "modules/d/1.gbin"), MOD("d", list(r, cm.record, NULL), 4));
    ROOT(list(r, b.record, d.record), 1, 1, false, false);
    put(r, path(r, "modules/unreachable/1.gbin"), "corrupt but not reachable", 25);
    yyjson_val *v = RUN(0);
    integer(r, v, "checks", 4);
    REQUIRE(
        yyjson_equals(get(node(r, v, "b", "1"), "targets"), get(node(r, v, "d", "1"), "targets")));
    REQUIRE(yyjson_arr_size(get(node(r, v, "app", "1"), "targets")) == 2);
}

static void versions(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *root = path(r, "root.gbin");
    module_t one = install(r, path(r, "modules/same/1.gbin"), MOD("same", NULL, 11));
    module_t two = install(r,
                           path(r, "modules/same/2.gbin"),
                           ARCH("same", "2", NULL, 22, OLD, 1, false, false, NULL));
    module_t b = install(r, path(r, "modules/b/1.gbin"), MOD("b", list(r, one.record, NULL), 1));
    ROOT(list(r, b.record, two.record), 100, 1, false, false);
    yyjson_val *v = run(r, c, root, 0, "run", remove_tree, (void *)path(r, "modules"), false);
    values(r, v, "same", "1", 11, 1);
    values(r, v, "same", "2", 22, 1);
}

static void invalid(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *root = path(r, "root.gbin"), *cp = path(r, "modules/c/1.gbin");
    for (int failure = 0; failure < 4; failure++) {
        module_t cm = MOD("c", NULL, 100);
        const char *record =
            failure == 2 ? "{\"name\":\"c\",\"version\":\"1\",\"hash\":\"" ZERO "\"}" : cm.record;
        module_t b =
            install(r,
                    path(r, "modules/b/1.gbin"),
                    ARCH("b", "1", list(r, record, NULL), 100, OLD, 1, false, false, "c@1"));
        ROOT(list(r, b.record, NULL), 100, 1, false, true);
        if (failure == 1)
            put(r, cp, "bad", 3);
        else if (failure == 2)
            install(r, cp, cm);
        else if (failure == 3)
            install(r, cp, MOD("wrong", NULL, 100));
        RUN(2);
        run(r, c, root, 2, "no-diagnostics", NULL, NULL, false);
    }
}

static void throwing(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *root = path(r, "root.gbin");
    ROOT(NULL, 100, 1, true, false);
    mutation_t m = {.root = root, .kind = 1};
    yyjson_val *v = run(r, c, root, 0, "run", mutate_files, &m, false);
    integer(r, node(r, v, "app", "1"), "failures", 1);
    values(r, v, "app", "1", 100, 1);
}

static void deep(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *root = path(r, "root.gbin");
    const char *dep = NULL;
    for (int j = 511; j >= 0; j--) {
        char name[32], file[80];
        snprintf(name, sizeof(name), "m%d", j);
        snprintf(file, sizeof(file), "modules/m%d/1.gbin", j);
        module_t m = install(r, path(r, file), MOD(name, dep ? list(r, dep, NULL) : NULL, j));
        dep = m.record;
    }
    ROOT(list(r, dep, NULL), 100, 1, false, false);
    yyjson_val *v = run(r, c, root, 0, "run", NULL, NULL, true);
    integer(r, v, "checks", 513);
    values(r, v, "m511", "1", 511, 1);
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
    fs_error_t ae = {0};
    if (!fs_arguments(&argc, &argv, &ae)) {
        clear_fs_error(&ae);
        return 1;
    }
#endif
    const char *ids[] = {"integration.module_snapshot.files",
                         "integration.module_snapshot.replace",
                         "integration.module_snapshot.delete",
                         "integration.module_snapshot.corrupt",
                         "integration.module_snapshot.diamond",
                         "integration.module_snapshot.versions",
                         "integration.module_snapshot.invalid",
                         "integration.module_snapshot.throw",
                         "integration.module_snapshot.deep"};
    scenario_body_t bodies[] = {files,
                                replace_case,
                                delete_case,
                                corrupt_case,
                                diamond,
                                versions,
                                invalid,
                                throwing,
                                deep};
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
        char *self = absolute_fs_path(argv[0], &e);
        clear_fs_error(&e);
        if (exe && vectors && self) {
            configuration_t c = {.exe = exe, .vectors = vectors, .self = self};
            scenario_definition_t defs[N(ids)];
            for (size_t j = 0; j < N(ids); j++)
                defs[j] =
                    (scenario_definition_t){ids[j], j == 8 ? 300000 : 180000, bodies[j], &c, NULL};
            scenario_options_t o = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(defs, N(ids), argc == 7 ? 2 : 1, selection, &o);
        }
        FREE(exe);
        FREE(vectors);
        FREE(self);
    } else
        fputs("usage: module_snapshot_scenarios ID HOST BUILD DATA VECTORS [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
