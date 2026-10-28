/** @file module_sources_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Source snapshot and canonical-path regression scenarios.
 */
#ifndef _WIN32
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_json.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <sys/stat.h>
#    include <unistd.h>
#endif
#define N(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    const char *exe;
    size_t count;
    yyjson_mut_doc *doc;
} configuration_t;

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool free_mut(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool free_heap(void *p) {
    free(p);
    return true;
}

static yyjson_val *get(yyjson_val *v, const char *k) {
    return yyjson_obj_get(v, k);
}

static yyjson_val *parse(scenario_run_t *r, const char *s, size_t n) {
    yyjson_doc *d = yyjson_read(s, n, 0);
    scenario_require(r, d != NULL, "valid JSON response");
    scenario_defer(r, free_doc, d);
    return yyjson_doc_get_root(d);
}

static const char *putn(scenario_run_t *r, const char *path, const void *s, size_t n) {
    /* mkdir each UTF-8 prefix, preserving literal POSIX backslashes. */
    char *parent = language_owned(r, ALLOC(strlen(path) + 1));
    strcpy(parent, path);
    for (char *p = parent + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            fs_error_t e = {0};
            fs_info_t info;
            bool exists = inspect_fs_path(parent, true, &info, &e);
            clear_fs_error(&e);
            if (!exists)
                language_directory(r, parent);
            else
                scenario_require(r, info.kind == FS_DIRECTORY, "fixture parent directory");
            *p = '/';
        }
    language_write(r, path, s, n);
    return path;
}

static const char *put(scenario_run_t *r, const char *path, const char *s) {
    return putn(r, path, s, strlen(s));
}

static void write_json(scenario_run_t *r, const char *path, yyjson_mut_val *v) {
    size_t n;
    char *s = yyjson_mut_val_write(v, 0, &n);
    scenario_require(r, s != NULL, "serialize descriptor");
    scenario_defer(r, free_heap, s);
    putn(r, path, s, n);
}

static yyjson_mut_val *
descriptor(scenario_run_t *r, configuration_t *c, const char *path, const char *main) {
    yyjson_mut_val *v = yyjson_mut_obj(c->doc);
    yyjson_mut_obj_add_uint(c->doc, v, "formatVersion", 1);
    yyjson_mut_obj_add_str(c->doc, v, "name", "sample");
    yyjson_mut_obj_add_str(c->doc, v, "version", "1.0.0");
    yyjson_mut_obj_add_strcpy(c->doc, v, "main", main ? main : "src/main.goat");
    write_json(r, path, v);
    return v;
}

static yyjson_val *execute(scenario_run_t *r,
                           configuration_t *c,
                           const char *path,
                           bool desc,
                           int status,
                           const char *cwd,
                           const char *a,
                           const char *b,
                           const char *d) {
    const char *args[] = {c->exe, desc ? "gmod" : "source", path, a, b, d, NULL};
    child_options_t o = {
        .executable = c->exe,
        .arguments = args,
        .working_directory = cwd,
        .environment =
            language_environment_override(r, "GOAT_MODULE_PATH", "/must/not/be/searched")};
    scenario_command_result_t result = scenario_command_capture(r, &o);
    c->count++;
    scenario_require(r, result.exit_code == status, "source graph process status");
    yyjson_val *v = parse(r, result.out, result.out_size), *s = get(v, "status");
    scenario_require(r,
                     yyjson_is_uint(s) && yyjson_get_uint(s) == (uint64_t)status,
                     "source graph JSON status");
    return v;
}

#define RUN(path, status) execute(r, c, path, true, status, NULL, NULL, NULL, NULL)

static void count(scenario_run_t *r, yyjson_val *v, size_t n) {
    scenario_require(r,
                     yyjson_is_arr(get(v, "files")) && yyjson_arr_size(get(v, "files")) == n,
                     "source file count");
}

static void shape(scenario_run_t *r, yyjson_val *v, const char *expected) {
    yyjson_val *e = parse(r, expected, strlen(expected)), *files = get(v, "files");
    count(r, v, yyjson_arr_size(e));
    for (size_t i = 0; i < yyjson_arr_size(e); i++) {
        yyjson_val *f = yyjson_arr_get(files, i), *x = yyjson_arr_get(e, i);
        const char *name = yyjson_get_str(get(f, "name")),
                   *want = yyjson_get_str(yyjson_arr_get(x, 0));
        scenario_require(r,
                         name && want && yyjson_get_len(get(f, "name")) == strlen(want)
                             && !strcmp(name, want),
                         "ordered source name");
        scenario_require(r,
                         yyjson_is_bool(get(f, "external"))
                             && yyjson_get_bool(get(f, "external"))
                                    == yyjson_get_bool(yyjson_arr_get(x, 1)),
                         "source external flag");
        yyjson_val *targets = get(f, "targets"), *et = yyjson_arr_get(x, 2);
        scenario_require(r,
                         yyjson_is_arr(targets) && yyjson_arr_size(targets) == yyjson_arr_size(et),
                         "source target count");
        for (size_t j = 0; j < yyjson_arr_size(et); j++) {
            yyjson_val *t = yyjson_arr_get(targets, j);
            scenario_require(r,
                             yyjson_is_uint(t)
                                 && yyjson_get_uint(t) == yyjson_get_uint(yyjson_arr_get(et, j)),
                             "ordered source target");
        }
    }
}

static bool link_file(scenario_run_t *r,
                      const char *target,
                      const char *alias,
                      bool symbolic,
                      bool privilege_skip) {
#ifdef _WIN32
    int nt = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, target, -1, NULL, 0),
        na = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, alias, -1, NULL, 0);
    scenario_require(r, nt > 0 && na > 0, "UTF-8 link paths");
    wchar_t *t = language_owned(r, ALLOC((size_t)nt * sizeof(*t))),
            *a = language_owned(r, ALLOC((size_t)na * sizeof(*a)));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, target, -1, t, nt);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, alias, -1, a, na);
    /* Match Python: permit developer-mode unprivileged symlinks where supported. */
    bool ok = symbolic ? CreateSymbolicLinkW(a, t, 2) != 0 : CreateHardLinkW(a, t, NULL) != 0;
    if (!ok && symbolic && GetLastError() == ERROR_INVALID_PARAMETER)
        ok = CreateSymbolicLinkW(a, t, 0) != 0;
    if (!ok && symbolic && privilege_skip && GetLastError() == ERROR_PRIVILEGE_NOT_HELD) {
        puts("SKIP symlink fixtures: Windows privilege not held");
        return false;
    }
#else
    (void)privilege_skip;
    bool ok = (symbolic ? symlink(target, alias) : link(target, alias)) == 0;
#endif
    scenario_require(r, ok, "create link fixture");
    return true;
}

static void body(scenario_run_t *r, void *user) {
    configuration_t *c = user;
    c->doc = yyjson_mut_doc_new(NULL);
    scenario_require(r, c->doc != NULL, "JSON arena");
    scenario_defer(r, free_mut, c->doc);
    const char *args[] = {c->exe, NULL};
    scenario_command_result_t api = language_command(r, c->exe, args, NULL);
    scenario_require(r, api.exit_code == 0, "source API checks");
    const char *root = scenario_work_directory(scenario_run_context(r));
#define PATH(s) language_join(r, root, s)
    const char *desc = PATH("project/app.gmod"), *main = PATH("project/src/main.goat"),
               *other = PATH("elsewhere");
    language_directory(r, other);
    descriptor(r, c, desc, NULL);
    put(r, main, "println(\"main\");");
    put(r, PATH("project/src/unused.goat"), "include \"missing\";");
    shape(r, RUN(desc, 0), "[[\"src/main.goat\",false,[]]]");
    put(r,
        desc,
        "{\"formatVersion\":1,\"name\":\"sample\",\"version\":\"1.0.0\",\"main\":\"src/"
        "main.goat\",\"dependencies\":{\"unused\":\"9.0.0\"},\"native\":{},\"description\":\"\","
        "\"authors\":[]}");
    yyjson_val *v = RUN(desc, 0), *dv = get(v, "descriptor"), *nv = get(dv, "native");
    const char *mode = yyjson_get_str(get(nv, "mode")), *target = yyjson_get_str(get(nv, "target"));
    scenario_require(r,
                     yyjson_is_obj(nv) && yyjson_obj_size(nv) == 2 && mode && target
                         && !strcmp(mode, "auto") && !strcmp(target, "host"),
                     "normalized native defaults");
    scenario_require(r,
                     !get(dv, "description") && !get(dv, "authors"),
                     "empty informational fields omitted");
    count(r, v, 1);
    descriptor(r, c, desc, NULL);
    put(r, main, "include \"vector.goat\"; if(false){include \"matrix.goat\";}");
    put(r, PATH("project/src/vector.goat"), "include \"common.goat\";");
    put(r, PATH("project/src/matrix.goat"), "func(){include \"common.goat\";}");
    put(r, PATH("project/src/common.goat"), "include \"main.goat\";");
    const char *graph = "[[\"src/main.goat\",false,[1,3]],[\"src/vector.goat\",false,[2]],[\"src/"
                        "common.goat\",false,[0]],[\"src/matrix.goat\",false,[2]]]";
    shape(r, RUN(desc, 0), graph);
    shape(r, execute(r, c, desc, true, 0, other, NULL, NULL, NULL), graph);
    put(r, main, "include \"./vector.goat\"; include \"../src/vector.goat\";");
    count(r, RUN(desc, 0), 3);
    put(r, PATH("shared/a/math.goat"), "");
    put(r, PATH("shared/b/math.goat"), "");
    put(r, main, "include \"../../shared/a/math.goat\"; include \"../../shared/b/math.goat\";");
    shape(r,
          RUN(desc, 0),
          "[[\"src/main.goat\",false,[1,2]],[\"math.goat\",true,[]],[\"math.goat\",true,[]]]");
    put(r, PATH("project-other/outer.goat"), "");
    put(r, main, "include \"../../project-other/outer.goat\";");
    scenario_require(
        r,
        yyjson_get_bool(get(yyjson_arr_get(get(RUN(desc, 0), "files"), 1), "external")),
        "prefix sibling is external");
    const char *second = PATH("elsewhere/second.gmod");
    descriptor(r, c, second, "../shared/a/math.goat");
    shape(r, RUN(second, 0), "[[\"math.goat\",true,[]]]");
    descriptor(r, c, desc, PATH("shared/a/math.goat"));
    shape(r, RUN(desc, 0), "[[\"math.goat\",true,[]]]");
    const char *paths[] = {"src/start",
                           "src/start.txt",
                           "$HOME/main.goat",
                           "~/main.goat",
                           "${HOME}/main.goat",
                           "%USERPROFILE%/main.goat",
                           "юникод/😀.goat",
#ifndef _WIN32
                           " main.goat ",
                           "   ",
                           "src\\literal.goat"
#endif
    };
    for (size_t i = 0; i < N(paths); i++) {
        put(r, language_join(r, PATH("project"), paths[i]), "");
        descriptor(r, c, desc, paths[i]);
        v = RUN(desc, 0);
        count(r, v, 1);
        yyjson_val *f = yyjson_arr_get(get(v, "files"), 0);
        scenario_require(
            r,
            yyjson_is_str(get(f, "name")) && yyjson_get_len(get(f, "name")) == strlen(paths[i])
                && !strcmp(yyjson_get_str(get(f, "name")), paths[i])
                && yyjson_is_bool(get(f, "external")) && !yyjson_get_bool(get(f, "external"))
                && yyjson_is_arr(get(f, "targets")) && yyjson_arr_size(get(f, "targets")) == 0,
            "literal main path");
    }
#ifdef _WIN32
    put(r, PATH("project/src/backslash.goat"), "");
    descriptor(r, c, desc, "src\\backslash.goat");
    shape(r, RUN(desc, 0), "[[\"src/backslash.goat\",false,[]]]");
    const char *badpaths[] = {"C:main.goat", "C:", "/src/main.goat", "\\src\\main.goat"};
    for (size_t i = 0; i < N(badpaths); i++) {
        descriptor(r, c, desc, badpaths[i]);
        RUN(desc, 2);
    }
#endif
    put(r, PATH("project/missing.goat"), "");
    descriptor(r, c, desc, "missing");
    RUN(desc, 2);
    const char *invalidmain[] = {"\"\"", "\"\\u0000x\"", "\"a\\u0000b\"", "\"a\\u0000\""};
    for (size_t i = 0; i < N(invalidmain); i++) {
        yyjson_mut_val *d = descriptor(r, c, desc, NULL);
        yyjson_val *m = parse(r, invalidmain[i], strlen(invalidmain[i]));
        yyjson_mut_obj_put(d, yyjson_mut_str(c->doc, "main"), yyjson_val_mut_copy(c->doc, m));
        write_json(r, desc, d);
        RUN(desc, 3);
    }
    const char *required[] = {"formatVersion", "name", "version", "main"};
    for (size_t i = 0; i < N(required); i++) {
        yyjson_mut_val *d = descriptor(r, c, desc, "missing");
        yyjson_mut_obj_remove_key(d, required[i]);
        write_json(r, desc, d);
        RUN(desc, 3);
    }
    const char *extras[] = {"{\"mode\":\"atuo\"}",
                            "{\"target\":42}",
                            "{\"unknown\":true}",
                            "{\"bad name\":\"1.0.0\"}",
                            "{\"valid\":\"bad version\"}",
                            "null"};
    for (size_t i = 0; i < N(extras); i++) {
        yyjson_mut_val *d = descriptor(r, c, desc, "missing");
        yyjson_val *x = parse(r, extras[i], strlen(extras[i]));
        yyjson_mut_obj_put(d,
                           yyjson_mut_str(c->doc, i == 3 || i == 4 ? "dependencies" : "native"),
                           yyjson_val_mut_copy(c->doc, x));
        write_json(r, desc, d);
        RUN(desc, 3);
    }
    put(r, desc, "{\"formatVersion\":1,\"formatVersion\":1}");
    RUN(desc, 3);
    putn(r, desc, "\xff", 1);
    RUN(desc, 3);
    descriptor(r, c, desc, NULL);
    put(r, PATH("project/src/a.goat"), "");
    put(r,
        main,
        "// include \"bad\"\n\"include \\\"bad\\\"\"; /* include \"bad\" */\ninclude \"a.goat\";");
    count(r, RUN(desc, 0), 2);
    put(r, main, "var obj; obj.include;");
    count(r, RUN(desc, 0), 1);
    const char *expr[] = {"\ninclude name;",
                          "\ninclude (\"a.goat\");",
                          "\ninclude \"a\" + \".goat\";",
                          "\ninclude \"\";",
                          "\ninclude \"a\\0.goat\";",
                          "\ninclude;",
                          "\ninclude 3;",
                          "\ninclude \"unterminated",
                          "\nif(false){include \"a.goat\";"};
    for (size_t i = 0; i < N(expr); i++) {
        put(r, main, expr[i]);
        v = RUN(desc, 4);
        scenario_require(r,
                         yyjson_get_uint(get(v, "row")) >= 2
                             && yyjson_get_uint(get(v, "column")) > 0,
                         "source error location");
    }
    put(r, main, "\n  include \"missing.goat\";");
    v = RUN(desc, 2);
    scenario_require(r,
                     yyjson_get_uint(get(v, "row")) == 2 && yyjson_get_uint(get(v, "column")) == 3,
                     "missing source exact location");
    putn(r, main, "\xff", 1);
    RUN(desc, 4);
    putn(r, main, "\0", 1);
    RUN(desc, 4);
    put(r, main, "");
    descriptor(r, c, desc, "src");
    RUN(desc, 2);
#ifndef _WIN32
    scenario_require(r, mkfifo(PATH("project/pipe"), 0600) == 0, "create FIFO");
    descriptor(r, c, desc, "pipe");
    RUN(desc, 2);
    put(r, main, "include \"../pipe\";");
    descriptor(r, c, desc, NULL);
    RUN(desc, 2);
    descriptor(r, c, desc, "/dev/null");
    RUN(desc, 2);
#endif
    const char *real_main = PATH("real/src/main.goat"), *real_desc = PATH("real/app.gmod"),
               *alias_desc = PATH("alias/app.gmod"), *alias_run = PATH("alias/run.goat");
    put(r, real_main, "include \"helper.goat\";");
    put(r, PATH("real/src/helper.goat"), "println(\"real\");");
    descriptor(r, c, real_desc, NULL);
    put(r, PATH("alias/src/helper.goat"), "include \"wrong\";");
    if (link_file(r, real_desc, alias_desc, true, true)
        && link_file(r, real_main, alias_run, true, true)) {
        const char *dg = "[[\"src/main.goat\",false,[1]],[\"src/helper.goat\",false,[]]]",
                   *sg = "[[\"main.goat\",false,[1]],[\"helper.goat\",false,[]]]";
        shape(r, execute(r, c, alias_desc, true, 0, other, NULL, NULL, NULL), dg);
        shape(r, RUN(real_desc, 0), dg);
        shape(r, execute(r, c, alias_run, false, 0, other, NULL, NULL, NULL), sg);
        shape(r, execute(r, c, real_main, false, 0, NULL, NULL, NULL, NULL), sg);
        shape(r, execute(r, c, alias_run, false, 0, NULL, NULL, NULL, NULL), sg);
        link_file(r, PATH("real/src/helper.goat"), PATH("real/src/again.goat"), true, false);
        put(r, real_main, "include \"helper.goat\"; include \"again.goat\";");
        shape(r,
              RUN(real_desc, 0),
              "[[\"src/main.goat\",false,[1,1]],[\"src/helper.goat\",false,[]]]");
        link_file(r, PATH("shared/a/math.goat"), PATH("real/src/external.goat"), true, false);
        put(r, real_main, "include \"external.goat\";");
        shape(r, RUN(real_desc, 0), "[[\"src/main.goat\",false,[1]],[\"math.goat\",true,[]]]");
    }
    const char *a = PATH("project/a/math.goat"), *b = PATH("project/b/math.goat");
    put(r, a, "include \"helper.goat\";");
    language_directory(r, PATH("project/b"));
    link_file(r, a, b, false, false);
    put(r, PATH("project/a/helper.goat"), "println(\"a\");");
    put(r, PATH("project/b/helper.goat"), "println(\"b\");");
    put(r, main, "include \"../a/math.goat\"; include \"../b/math.goat\";");
    descriptor(r, c, desc, NULL);
    shape(r,
          RUN(desc, 0),
          "[[\"src/main.goat\",false,[1,3]],[\"a/math.goat\",false,[2]],[\"a/"
          "helper.goat\",false,[]],[\"b/math.goat\",false,[4]],[\"b/helper.goat\",false,[]]]");
    execute(r, c, desc, true, 5, NULL, "2", NULL, NULL);
    execute(r, c, desc, true, 2, NULL, "100", "8", NULL);
    execute(r, c, desc, true, 2, NULL, "100", "1000", "5");
    for (size_t i = 0; i < 1800; i++) {
        char name[64], text[64];
        snprintf(name, sizeof(name), "deep/%zu.goat", i);
        snprintf(text, sizeof(text), "include \"%zu.goat\";", (i + 1) % 1800);
        put(r, PATH(name), text);
    }
    v = execute(r, c, PATH("deep/0.goat"), false, 0, NULL, NULL, NULL, NULL);
    count(r, v, 1800);
    yyjson_val *last = get(yyjson_arr_get(get(v, "files"), 1799), "targets");
    scenario_require(r,
                     yyjson_arr_size(last) == 1 && yyjson_is_uint(yyjson_arr_get(last, 0))
                         && yyjson_get_uint(yyjson_arr_get(last, 0)) == 0,
                     "deep cycle closes");
    put(r, main, "exit(4); println(\"MUST NOT RUN\"); if(false){include \"a.goat\";}");
    count(r, RUN(desc, 0), 2);
    printf("%zu source graph fixtures passed\n", c->count);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t ae = {0};
    if (!fs_arguments(&argc, &argv, &ae)) {
        clear_fs_error(&ae);
        return 1;
    }
#endif
    int result = 1;
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        puts("integration.module_sources");
        result = 0;
    } else if (argc == 5 || argc == 6) {
        fs_error_t e = {0};
        char *exe = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        if (exe) {
            configuration_t c = {.exe = exe};
            scenario_definition_t definition = {"integration.module_sources",
                                                180000,
                                                body,
                                                &c,
                                                NULL};
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 6 ? argv[5] : NULL};
            result = scenario_registry_main(&definition, 1, argc == 6 ? 2 : 1, selection, &options);
        }
        FREE(exe);
    } else
        fputs("usage: module_sources_scenarios ID HOST BUILD DATA [--keep-work]\n", stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
