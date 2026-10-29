/** @file module_identity_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Module identity and first-archive native ownership (#354).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/module_snapshot_fixture.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>
#define N(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    const char *exe, *vectors;
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

static bool free_mut(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static yyjson_mut_doc *mut(scenario_run_t *r, const void *s, size_t n) {
    yyjson_mut_doc *d = format_json_parse(s, n, NULL, 0);
    scenario_require(r, d != NULL, "fixture JSON");
    scenario_defer(r, free_mut, d);
    return d;
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

/* Rebuild independent ZIP entries while keeping the core identity unchanged. */
static module_t
rewrite(scenario_run_t *r, module_t m, bool no_native, const char *replacement, bool extra) {
    size_t n;
    char error[256] = {0};
    unsigned char *raw =
        owned(r, format_zip_extract(m.data, m.size, 0, 1024 * 1024, &n, error, sizeof(error)));
    yyjson_mut_doc *d = mut(r, raw, n);
    yyjson_mut_val *v = yyjson_mut_doc_get_root(d);
    unsigned char *code =
        owned(r, format_zip_extract(m.data, m.size, 1, 1024 * 1024, &n, error, sizeof(error)));
    size_t code_size = n;
    format_zip_entry_t entries[4];
    size_t count = 2;
    yyjson_mut_val *native = yyjson_mut_obj_get(v, "native");
    if (no_native)
        yyjson_mut_obj_remove_key(v, "native");
    else if (native) {
        const char *library = replacement;
        size_t lib_size;
        if (library)
            lib_size = strlen(library);
        else
            library = owned(r,
                            format_zip_extract(m.data,
                                               m.size,
                                               2,
                                               1024 * 1024,
                                               &lib_size,
                                               error,
                                               sizeof(error)));
        yyjson_mut_val *first = yyjson_mut_arr_get_first(native);
        char hash[65];
        format_binary_sha256(library, lib_size, hash);
        REQUIRE(yyjson_mut_obj_put(first, yyjson_mut_str(d, "hash"), yyjson_mut_strcpy(d, hash)));
        format_zip_entry_init(entries + count++, "native/opaque.so", library, lib_size, false);
        if (extra) {
            yyjson_mut_val *additional = yyjson_mut_val_mut_copy(d, first);
            yyjson_mut_val *target = yyjson_mut_obj_get(additional, "target");
            REQUIRE(yyjson_mut_obj_put(target,
                                       yyjson_mut_str(d, "architecture"),
                                       yyjson_mut_str(d, "aarch64")));
            const char *features[] = {"fp", "neon"};
            REQUIRE(yyjson_mut_obj_put(target,
                                       yyjson_mut_str(d, "cpuFeatures"),
                                       yyjson_mut_arr_with_str(d, features, 2)));
            REQUIRE(yyjson_mut_obj_put(additional,
                                       yyjson_mut_str(d, "library"),
                                       yyjson_mut_str(d, "native/arm.so")));
            const char *arm = "later ARM library";
            format_binary_sha256(arm, strlen(arm), hash);
            REQUIRE(yyjson_mut_obj_put(additional,
                                       yyjson_mut_str(d, "hash"),
                                       yyjson_mut_strcpy(d, hash)));
            REQUIRE(yyjson_mut_arr_append(native, additional));
            format_zip_entry_init(entries + count++, "native/arm.so", arm, strlen(arm), false);
        }
    }
    REQUIRE(yyjson_mut_obj_put(
        v,
        yyjson_mut_str(d, "description"),
        yyjson_mut_str(d, "Physical copy: metadata excluded from main identity")));
    char *json = owned(r, yyjson_mut_write(d, 0, &n));
    format_zip_entry_init(entries, "module.json", json, n, false);
    format_zip_entry_init(entries + 1, "bytecode.bin", code, code_size, false);
    for (size_t j = 0; j < count; j++)
        entries[j].flags = 0;
    m.data =
        owned(r, format_zip_build(entries, count, NULL, 0, false, &m.size, error, sizeof(error)));
    return m;
}

static module_t fixture(scenario_run_t *r,
                        configuration_t *c,
                        const char *name,
                        const char *version,
                        const char *deps,
                        int value,
                        bool native) {
    return snapshot_fixture_archive(r,
                                    c->vectors,
                                    &c->schema,
                                    name,
                                    version,
                                    deps,
                                    value,
                                    OLD,
                                    1,
                                    false,
                                    native,
                                    NULL);
}

#define MOD(name, deps, value) fixture(r, c, name, "1", deps, value, false)

static void put_module(scenario_run_t *r, const char *where, module_t m) {
    install(r, path(r, where), m);
}

static yyjson_val *run(scenario_run_t *r, configuration_t *c, int status) {
    const char *args[] = {c->exe, path(r, "root.gbin"), path(r, "shared"), NULL};
    scenario_command_result_t out = language_command(r, c->exe, args, NULL);
    REQUIRE(out.exit_code == 0);
    yyjson_val *v = parse(r, out.out, out.out_size);
    integer(r, v, "status", status);
    if (!status)
        integer(r, v, "checks", (int)yyjson_arr_size(get(v, "nodes")));
    return v;
}

static void copies(scenario_run_t *r, configuration_t *c, int kind) {
    module_t shared = MOD("shared", NULL, 1);
    module_t a = MOD("a", list(r, shared.record, NULL), 100);
    module_t b = MOD("b", list(r, shared.record, NULL), 100);
    put_module(r, "root.gbin", MOD("app", list(r, a.record, b.record), 100));
    put_module(r, "modules/a/1.gbin", a);
    put_module(r, "modules/b/1.gbin", b);
    put_module(r, "modules/shared/1.gbin", shared);
    if (kind == 1)
        shared = rewrite(r, shared, false, NULL, false);
    if (kind == 2) {
        shared.data = "bad";
        shared.size = 3;
    }
    put_module(r, "shared/shared/1.gbin", shared);
    yyjson_val *v = run(r, c, 0);
    REQUIRE(yyjson_arr_size(get(v, "nodes")) == 4);
    integer(r, node(r, v, "shared", "1"), "counter", 2);
    REQUIRE(
        yyjson_equals(get(node(r, v, "a", "1"), "targets"), get(node(r, v, "b", "1"), "targets")));
}

static void identical(scenario_run_t *r, void *arg) {
    copies(r, arg, 0);
}

static void information(scenario_run_t *r, void *arg) {
    copies(r, arg, 1);
}

static void corrupt_unused(scenario_run_t *r, void *arg) {
    copies(r, arg, 2);
}

static void versions(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    module_t one = MOD("same", NULL, 11), two = fixture(r, c, "same", "2", NULL, 11, false);
    module_t a = MOD("a", list(r, one.record, NULL), 100),
             b = MOD("b", list(r, one.record, NULL), 100);
    char *deps = owned(r, malloc(strlen(a.record) + strlen(b.record) + strlen(two.record) + 5));
    sprintf(deps, "[%s,%s,%s]", a.record, b.record, two.record);
    put_module(r, "root.gbin", MOD("app", deps, 100));
    put_module(r, "modules/same/1.gbin", one);
    put_module(r, "modules/same/2.gbin", two);
    put_module(r, "modules/a/1.gbin", a);
    put_module(r, "modules/b/1.gbin", b);
    yyjson_val *v = run(r, c, 0), *n1 = node(r, v, "same", "1"), *n2 = node(r, v, "same", "2");
    integer(r, n1, "counter", 2);
    integer(r, n2, "counter", 1);
    REQUIRE(strcmp(string(r, get(n1, "hash")), string(r, get(n2, "hash"))));
}

static void native_case(scenario_run_t *r, configuration_t *c, int kind) {
    module_t full = fixture(r, c, "library", "1", NULL, 100, true);
    put_module(r, "root.gbin", MOD("app", list(r, full.record, NULL), 100));
    module_t first = kind == 0 ? rewrite(r, full, true, NULL, false) : full;
    if (kind < 4)
        put_module(r, "modules/library/1.gbin", first);
    module_t later = kind == 1 ? rewrite(r, full, true, NULL, false)
                               : rewrite(r, full, false, "later library", kind == 2 || kind == 5);
    if (kind == 3) {
        later.data = "corrupt";
        later.size = 7;
    }
    put_module(r, "shared/library/1.gbin", later);
    yyjson_val *v = run(r, c, 0), *lib = node(r, v, "library", "1");
    yyjson_val *record = parse(r, full.record, strlen(full.record));
    REQUIRE(!strcmp(string(r, get(lib, "hash")), string(r, get(record, "hash"))));
    const char *expected = kind == 0   ? "[]"
                           : kind == 4 ? "[\"later library\"]"
                           : kind == 5 ? "[\"later ARM library\",\"later library\"]"
                                       : "[\"never OS-load: старый 😀\"]";
    REQUIRE(yyjson_equals(get(lib, "native"), parse(r, expected, strlen(expected))));
}

static void vm_first(scenario_run_t *r, void *arg) {
    native_case(r, arg, 0);
}

static void native_first(scenario_run_t *r, void *arg) {
    native_case(r, arg, 1);
}

static void different_native(scenario_run_t *r, void *arg) {
    native_case(r, arg, 2);
}

static void corrupt_later(scenario_run_t *r, void *arg) {
    native_case(r, arg, 3);
}

static void fallback_missing(scenario_run_t *r, void *arg) {
    native_case(r, arg, 4);
}

static void extra_fallback(scenario_run_t *r, void *arg) {
    native_case(r, arg, 5);
}

static void mismatching_first(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    module_t good = MOD("library", NULL, 3);
    put_module(r, "root.gbin", MOD("app", list(r, good.record, NULL), 100));
    put_module(r, "modules/library/1.gbin", MOD("library", NULL, 4));
    put_module(r, "shared/library/1.gbin", good);
    run(r, c, 2);
}

static void hash_conflict(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    module_t good = MOD("library", NULL, 3), other = MOD("library", NULL, 4);
    module_t a = MOD("a", list(r, good.record, NULL), 100),
             b = MOD("b", list(r, other.record, NULL), 100);
    put_module(r, "root.gbin", MOD("app", list(r, a.record, b.record), 100));
    put_module(r, "modules/a/1.gbin", a);
    put_module(r, "modules/b/1.gbin", b);
    put_module(r, "modules/library/1.gbin", good);
    run(r, c, 2);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t ae = {0};
    if (!fs_arguments(&argc, &argv, &ae)) {
        clear_fs_error(&ae);
        return 1;
    }
#endif
    const char *ids[] = {"integration.module_identity.identical",
                         "integration.module_identity.information",
                         "integration.module_identity.corrupt-unused",
                         "integration.module_identity.versions",
                         "integration.module_identity.vm-first",
                         "integration.module_identity.native-first",
                         "integration.module_identity.different-native",
                         "integration.module_identity.corrupt-later",
                         "integration.module_identity.fallback-missing",
                         "integration.module_identity.extra-fallback",
                         "integration.module_identity.mismatching-first",
                         "integration.module_identity.hash-conflict"};
    scenario_body_t bodies[] = {identical,
                                information,
                                corrupt_unused,
                                versions,
                                vm_first,
                                native_first,
                                different_native,
                                corrupt_later,
                                fallback_missing,
                                extra_fallback,
                                mismatching_first,
                                hash_conflict};
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
            configuration_t c = {.exe = exe, .vectors = vectors};
            scenario_definition_t defs[N(ids)];
            for (size_t j = 0; j < N(ids); j++)
                defs[j] = (scenario_definition_t){ids[j], 180000, bodies[j], &c, NULL};
            scenario_options_t o = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(defs, N(ids), argc == 7 ? 2 : 1, selection, &o);
        }
        FREE(exe);
        FREE(vectors);
    } else
        fputs("usage: module_identity_scenarios ID HOST BUILD DATA VECTORS [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
