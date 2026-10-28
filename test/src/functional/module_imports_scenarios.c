/** @file module_imports_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Static imports, defaults, source sites and deterministic ordering.
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
#define N(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    const char *exe;
    size_t count;
} configuration_t;

static bool free_doc(void *p) {
    yyjson_doc_free(p);
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

static void descriptor(scenario_run_t *r, const char *path, const char *defaults) {
    char text[1024];
    snprintf(text,
             sizeof(text),
             "{\"formatVersion\":1,\"name\":\"sample\",\"version\":\"1.0.0\","
             "\"main\":\"src/main.goat\"%s%s}",
             defaults ? ",\"dependencies\":" : "",
             defaults ? defaults : "");
    put(r, path, text);
}

static yyjson_val *execute(scenario_run_t *r,
                           configuration_t *c,
                           const char *path,
                           bool desc,
                           int status,
                           const char *limit,
                           const char *cwd) {
    const char *args[] = {c->exe, desc ? "gmod" : "source", path, limit, NULL};
    child_options_t o = {
        .executable = c->exe,
        .arguments = args,
        .working_directory = cwd,
        .environment =
            language_environment_override(r, "GOAT_MODULE_PATH", "/must/not/be/searched")};
    scenario_command_result_t result = scenario_command_capture(r, &o);
    c->count++;
    scenario_require(r, result.exit_code == status, "import process status");
    yyjson_val *v = parse(r, result.out, result.out_size), *s = get(v, "status");
    scenario_require(r,
                     yyjson_is_uint(s) && yyjson_get_uint(s) == (uint64_t)status,
                     "import JSON status");
    if (status) {
        scenario_require(r,
                         yyjson_is_str(get(v, "message")) && yyjson_get_len(get(v, "message")),
                         "import failure diagnostic");
        scenario_require(r,
                         !get(v, "dependencies")
                             || (yyjson_is_arr(get(v, "dependencies"))
                                 && !yyjson_arr_size(get(v, "dependencies"))),
                         "failure has no dependencies");
    }
    return v;
}

#define RUN(path, status) execute(r, c, path, true, status, NULL, NULL)

static void equal(scenario_run_t *r, yyjson_val *v, const char *json) {
    scenario_require(r, yyjson_equals(v, parse(r, json, strlen(json))), "exact JSON value");
}

static void pairs(scenario_run_t *r, yyjson_val *v, const char *key, const char *json) {
    yyjson_val *a = get(v, key), *e = parse(r, json, strlen(json));
    scenario_require(r, yyjson_is_arr(a) && yyjson_arr_size(a) == yyjson_arr_size(e), "pair count");
    for (size_t i = 0; i < yyjson_arr_size(e); i++) {
        yyjson_val *d = yyjson_arr_get(a, i), *p = yyjson_arr_get(e, i);
        scenario_require(r,
                         yyjson_equals(get(d, "name"), yyjson_arr_get(p, 0))
                             && yyjson_equals(get(d, "version"), yyjson_arr_get(p, 1)),
                         "ordered dependency pair");
    }
}

static yyjson_val *sites(scenario_run_t *r, yyjson_val *v, const char *name, const char *version) {
    yyjson_val *a = get(v, "dependencies");
    for (size_t i = 0; i < yyjson_arr_size(a); i++) {
        yyjson_val *d = yyjson_arr_get(a, i);
        if (yyjson_equals_str(get(d, "name"), name)
            && yyjson_equals_str(get(d, "version"), version)) {
            scenario_require(r, yyjson_is_arr(get(d, "sites")), "sites array");
            return get(d, "sites");
        }
    }
    scenario_require(r, false, "dependency sites found");
    return NULL;
}

static void location(scenario_run_t *r,
                     yyjson_val *v,
                     unsigned file,
                     unsigned row,
                     unsigned column,
                     unsigned offset) {
    const char *keys[] = {"file", "row", "column", "offset"};
    unsigned values[] = {file, row, column, offset};
    for (size_t i = 0; i < N(keys); i++)
        scenario_require(r,
                         yyjson_is_uint(get(v, keys[i]))
                             && yyjson_get_uint(get(v, keys[i])) == values[i],
                         "exact source location");
}

static void body(scenario_run_t *r, void *user) {
    configuration_t *c = user;
    const char *args[] = {c->exe, NULL};
    scenario_require(r,
                     language_command(r, c->exe, args, NULL).exit_code == 0,
                     "import API checks");
    const char *root = scenario_work_directory(scenario_run_context(r));
#define PATH(s) language_join(r, root, s)
    const char *desc = PATH("project/app.gmod"), *main = PATH("project/src/main.goat"),
               *other = PATH("elsewhere");
    language_directory(r, other);
    descriptor(r, desc, NULL);
    put(r, main, "");
    pairs(r, RUN(desc, 0), "dependencies", "[]");
    descriptor(r, desc, "{\"zeta\":\"9\",\"alpha\":\"1\"}");
    yyjson_val *v = RUN(desc, 0);
    pairs(r, v, "dependencies", "[]");
    pairs(r, v, "unused_defaults", "[[\"alpha\",\"1\"],[\"zeta\",\"9\"]]");
    put(r,
        main,
        "exit(4); println(\"MUST NOT RUN\");\n"
        "if(false){import \"zeta\";}\n"
        "func(){import \"alpha@1\";}\n"
        "import \"alpha\"; import \"alpha@2\"; import \"alpha@1\";");
    v = RUN(desc, 0);
    pairs(r, v, "dependencies", "[[\"alpha\",\"1\"],[\"alpha\",\"2\"],[\"zeta\",\"9\"]]");
    scenario_require(r, yyjson_arr_size(sites(r, v, "alpha", "1")) == 3, "three exact alpha sites");
    pairs(r, v, "unused_defaults", "[]");
    scenario_require(r,
                     yyjson_equals(v, execute(r, c, desc, true, 0, NULL, other)),
                     "cwd independent response");
    put(r, main, "import \"alpha@2\";");
    v = RUN(desc, 0);
    pairs(r, v, "dependencies", "[[\"alpha\",\"2\"]]");
    pairs(r, v, "unused_defaults", "[[\"alpha\",\"1\"],[\"zeta\",\"9\"]]");
    const char *missing[] = {"import \"sample\";",
                             "import \"missing\";",
                             "import \"absent@7\"; import \"absent\";"};
    for (size_t i = 0; i < N(missing); i++) {
        put(r, main, missing[i]);
        RUN(desc, 5);
    }
    put(r, main, "import \"alpha\";");
    execute(r, c, main, false, 5, NULL, NULL);
    put(r, main, "import \"alpha@1\";");
    pairs(r, execute(r, c, main, false, 0, NULL, NULL), "dependencies", "[[\"alpha\",\"1\"]]");

    put(r,
        main,
        "// import \"missing\"\n/* import variable; */\n"
        "\"import \\\"missing\\\"\"; obj.import; obj /* comment */ . import;\n"
        "imported; imports; import \"alpha@1\";");
    pairs(r, RUN(desc, 0), "dependencies", "[[\"alpha\",\"1\"]]");
    put(r, main, "// 😀é\n  import \"alpha@1\";\nimport \"alpha@1\";");
    v = RUN(desc, 0);
    equal(r,
          sites(r, v, "alpha", "1"),
          "[{\"file\":0,\"row\":2,\"column\":3,\"offset\":8},"
          "{\"file\":0,\"row\":3,\"column\":1,\"offset\":26}]");
    const char *escaped[] = {"a\\tb@1", "a\\nb@1", "a\\\"b@1", "a\\\\b@1"};
    char text[1024];
    for (size_t i = 0; i < N(escaped); i++) {
        snprintf(text, sizeof(text), "import \"%s\";", escaped[i]);
        put(r, main, text);
        RUN(desc, 4);
    }
    const char *specs[] = {"zeta@1", "alpha@2", "alpha@10"};
    for (size_t i = 0; i < 3; i++)
        for (size_t j = 0; j < 3; j++)
            if (i != j) {
                size_t k = 3 - i - j;
                snprintf(text,
                         sizeof(text),
                         "import \"%s\";import \"%s\";import \"%s\";",
                         specs[i],
                         specs[j],
                         specs[k]);
                put(r, main, text);
                pairs(r,
                      RUN(desc, 0),
                      "dependencies",
                      "[[\"alpha\",\"10\"],[\"alpha\",\"2\"],[\"zeta\",\"1\"]]");
            }
    put(r, main, "import \"alpha@1.02\"; import \"alpha@1.2\"; import \"alpha@latest\";");
    pairs(r,
          RUN(desc, 0),
          "dependencies",
          "[[\"alpha\",\"1.02\"],[\"alpha\",\"1.2\"],[\"alpha\",\"latest\"]]");
    put(r, PATH("project/src/unused.goat"), "import \"missing\";");
    put(r, PATH("project/src/helper.goat"), "import \"shared@1\"; include \"main.goat\";");
    put(r, PATH("external.goat"), "import \"external@3\"; import \"shared@1\";");
    put(r,
        main,
        "import \"shared@1\"; include \"helper.goat\"; include \"./helper.goat\";"
        "if(false){include \"../../external.goat\";}");
    v = RUN(desc, 0);
    equal(r, get(v, "files"), "[\"src/main.goat\",\"src/helper.goat\",\"external.goat\"]");
    pairs(r, v, "dependencies", "[[\"external\",\"3\"],[\"shared\",\"1\"]]");
    yyjson_val *s = sites(r, v, "shared", "1");
    scenario_require(r, yyjson_arr_size(s) == 3, "shared sites count");
    for (size_t i = 0; i < 3; i++)
        scenario_require(r,
                         yyjson_is_uint(get(yyjson_arr_get(s, i), "file"))
                             && yyjson_get_uint(get(yyjson_arr_get(s, i), "file")) == i,
                         "ordered shared files");
    scenario_require(r,
                     yyjson_arr_size(get(execute(r, c, main, false, 0, NULL, NULL), "files")) == 3,
                     "standalone include graph");
    put(r, PATH("project/src/helper.goat"), "\n  import \"missing\";");
    location(r, RUN(desc, 5), 1, 2, 3, 3);
    put(r, main, "import \"alpha@1\"; import \"alpha@1\";");
    scenario_require(
        r,
        yyjson_arr_size(sites(r, execute(r, c, desc, true, 0, "2", NULL), "alpha", "1")) == 2,
        "exact import limit");
    v = execute(r, c, desc, true, 6, "1", NULL);
    scenario_require(r,
                     yyjson_get_uint(get(v, "offset")) == strlen("import \"alpha@1\"; "),
                     "limit error offset");
    scenario_require(
        r,
        yyjson_arr_size(sites(r, execute(r, c, desc, true, 0, "0", NULL), "alpha", "1")) == 2,
        "zero import limit default");
    const char *line = "import \"alpha@1\";\n";
    size_t length = strlen(line);
    char *large = language_owned(r, ALLOC(65537 * length + 1));
    for (size_t i = 0; i < 65537; i++)
        memcpy(large + i * length, line, length);
    putn(r, main, large, 65536 * length);
    scenario_require(r,
                     yyjson_arr_size(sites(r, RUN(desc, 0), "alpha", "1")) == 65536,
                     "default boundary accepted");
    putn(r, main, large, 65537 * length);
    scenario_require(r,
                     yyjson_get_uint(get(RUN(desc, 6), "row")) == 65537,
                     "default boundary rejected");
    size_t used = 0;
    for (size_t i = 0; i < 16384; i++)
        used +=
            (size_t)snprintf(large + used, 65537 * length + 1 - used, "import \"m%zu@1\";\n", i);
    putn(r, main, large, used);
    scenario_require(r,
                     yyjson_arr_size(get(RUN(desc, 0), "dependencies")) == 16384,
                     "distinct dependency boundary");
    const char *invalid[] = {"import variable;",
                             "import (\"alpha@1\");",
                             "import 3;",
                             "import;",
                             "import",
                             "import \"\";",
                             "import \"a\\0b\";",
                             "import \"alpha\" + \"@1\";",
                             "import \"alpha\" \"@1\";",
                             "import \"alpha@1\"();"};
    for (size_t i = 0; i < N(invalid); i++) {
        snprintf(text, sizeof(text), "\n  %s", invalid[i]);
        put(r, main, text);
        v = RUN(desc, 3);
        scenario_require(r,
                         yyjson_get_uint(get(v, "row")) == 2
                             && yyjson_get_uint(get(v, "column")) >= 3,
                         "syntax failure location");
    }
    char long_name[259], long_version[254];
    memset(long_name, 'a', 256);
    memcpy(long_name + 256, "@1", 3);
    memcpy(long_version, "a@", 2);
    memset(long_version + 2, '1', 251);
    long_version[253] = 0;
    const char *bad_spec[] = {"a@",
                              "@1",
                              "a@1@2",
                              "Upper@1",
                              "a@Upper",
                              "a/b@1",
                              "a b@1",
                              "con@1",
                              "a@con",
                              "a.@1",
                              "é@1",
                              "-a@1",
                              long_name,
                              long_version};
    for (size_t i = 0; i < N(bad_spec); i++) {
        snprintf(text, sizeof(text), "\n  import \"%s\";", bad_spec[i]);
        put(r, main, text);
        location(r, RUN(desc, 4), 0, 2, 3, 3);
    }
    putn(r, main, "\xff", 1);
    RUN(desc, 104);
    putn(r, main, "\0", 1);
    RUN(desc, 104);
    put(r, main, "import \"unterminated");
    RUN(desc, 104);
    put(r, main, "if(false){import \"a@1\";");
    RUN(desc, 104);
    put(r, main, "import \"alpha@2\";");
    const char *bad_defaults[] = {"{\"bad name\":\"1\"}",
                                  "{\"alpha\":\"bad version\"}",
                                  "{\"alpha\":1}"};
    for (size_t i = 0; i < N(bad_defaults); i++) {
        descriptor(r, desc, bad_defaults[i]);
        RUN(desc, 103);
    }
    printf("%zu static import fixtures passed\n", c->count);
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
        puts("integration.module_imports");
        result = 0;
    } else if (argc == 5 || argc == 6) {
        fs_error_t e = {0};
        char *exe = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        if (exe) {
            configuration_t c = {.exe = exe};
            scenario_definition_t definition = {"integration.module_imports",
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
        fputs("usage: module_imports_scenarios ID HOST BUILD DATA [--keep-work]\n", stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
