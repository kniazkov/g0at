#ifndef _WIN32
#    define _POSIX_C_SOURCE 200809L
#endif
/** @file module_execution_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real compile/run and negative helper contract tests.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/module_execution.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

typedef struct {
    const char *scenario, *exe, *self;
    bool reached;
    char *work;
} state_t;

static int child(int argc, char **argv) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    const char *mode = argv[1];
    if (!strcmp(mode, "fixture-env")) {
        const char *value = getenv("GOAT_MODULE_PATH");
        if (!value || argc < 3 || strcmp(value, argv[2]))
            return 9;
        const char *inherited = getenv("GOAT_HELPER_INHERITED");
        if (!inherited || strcmp(inherited, "retained value"))
            return 11;
        FILE *f = FOPEN("cwd-marker", "wb");
        if (!f)
            return 10;
        fclose(f);
        fputs(value, stdout);
        return 0;
    }
    if (!strcmp(mode, "fixture-large")) {
        for (size_t i = 0; i < 100000; ++i)
            fputc('x', stdout);
        fwrite(" a\0b\r\n\r\n", 1, 9, stdout);
        return 0;
    }
    if (!strcmp(mode, "fixture-utf8")) {
        fputc(0xff, stdout);
        return 0;
    }
    if (!strcmp(mode, "fixture-crlf")) {
        fputs("a\r\n", stdout);
        return 0;
    }
    if (!strcmp(mode, "fixture-fail")) {
        fputs("expected diagnostic", stderr);
        return 7;
    }
    if (!strcmp(mode, "fixture-silent"))
        return 7;
    return 0;
}

static bool release(void *p) {
    FREE(p);
    return true;
}

static void body(scenario_run_t *run, void *opaque) {
    state_t *state = opaque;
    const char *work = scenario_work_directory(scenario_run_context(run));
    state->work = malloc(strlen(work) + 1);
    scenario_require(run, state->work != NULL, "cannot remember workspace");
    strcpy(state->work, work);
    bool real = !strcmp(state->scenario, "real") || !strcmp(state->scenario, "compile-failure");
    module_execution_suite_t *suite = module_execution_suite(run, real ? state->exe : state->self);
    module_execution_project_t *p = module_execution_project(suite, "project with spaces");
    if (!strcmp(state->scenario, "real")) {
        module_execution_file_t files[] = {{"nested/other.goat", "public answer = 42;"}};
        module_execution_compile(p,
                                 "library",
                                 "external answer = (include \"nested/other.goat\").answer;",
                                 "2",
                                 files,
                                 1,
                                 "{}",
                                 false);
        const char *expected = "42\n a\0b \n\n";
        const char *code = "const lib = import \"library\"; println(lib.answer); println(\" a\\0b "
                           "\" ); println(\"\");";
        const char *app =
            module_execution_compile(p, "app", code, NULL, NULL, 0, "{\"library\":\"2\"}", false);
        module_execution_invoke(p, "binary", app, NULL, expected, 10, false);
        scenario_require(run, module_execution_count(suite) == 3, "command count");
        module_execution_project_t *second = module_execution_project(suite, "isolated");
        scenario_require(run,
                         strcmp(module_execution_modules(p), module_execution_modules(second)) != 0,
                         "projects share modules");
        module_execution_run(second, "println(7);", "7\n", 2);
    } else if (!strcmp(state->scenario, "compile-failure")) {
        module_execution_compile(p, "broken", "const = ;", NULL, NULL, 0, NULL, true);
    } else if (!strcmp(state->scenario, "capture")) {
        unsigned char *expected = ALLOC(100009);
        scenario_defer(run, release, expected);
        memset(expected, 'x', 100000);
        memcpy(expected + 100000, " a\0b\n\n", 7);
        scenario_command_result_t r =
            module_execution_invoke(p, "fixture-large", "unused", NULL, expected, 100007, false);
        scenario_require(run, r.out_size == 100009 && r.err_size == 0, "raw output lost");
        module_execution_invoke(p, "fixture-fail", "unused", NULL, "", 0, true);
    } else if (!strcmp(state->scenario, "environment")) {
        const char *modules = module_execution_modules(p);
        module_execution_invoke(p, "fixture-env", modules, NULL, modules, strlen(modules), false);
        fs_error_t error = {0};
        fs_info_t info;
        char *path = join_fs_path(scenario_work_directory(scenario_run_context(run)),
                                  "unrelated/cwd-marker",
                                  &error);
        bool ok = path && inspect_fs_path(path, false, &info, &error);
        FREE(path);
        clear_fs_error(&error);
        scenario_require(run, ok, "child did not use unrelated cwd");
    } else if (!strcmp(state->scenario, "mismatch")) {
        module_execution_invoke(p, "fixture-crlf", "unused", NULL, "b\n", 2, false);
    } else if (!strcmp(state->scenario, "expected-crlf")) {
        module_execution_invoke(p, "fixture-crlf", "unused", NULL, "a\r\n", 3, false);
    } else if (!strcmp(state->scenario, "invalid-utf8")) {
        const unsigned char expected[] = {255};
        module_execution_invoke(p, "fixture-utf8", "unused", NULL, expected, 1, false);
    } else if (!strcmp(state->scenario, "missing-diagnostic")) {
        module_execution_invoke(p, "fixture-silent", "unused", NULL, "", 0, true);
    } else if (!strcmp(state->scenario, "unexpected-success")) {
        module_execution_invoke(p, "fixture-ok", "unused", NULL, "", 0, true);
    } else if (!strcmp(state->scenario, "unexpected-failure")) {
        module_execution_invoke(p, "fixture-fail", "unused", NULL, "", 0, false);
    } else if (!strcmp(state->scenario, "invalid-path")) {
        module_execution_put(p, "../escape", "no");
    } else
        scenario_require(run, false, "unknown scenario");
    state->reached = true;
}

int main(int argc, char **argv) {
    if (argc > 1 && !strncmp(argv[1], "fixture-", 8))
        return child(argc, argv);
    if (argc < 5 || argc > 6)
        return 2;
    bool keep = argc == 6 && scenario_keep_work_argument(argv[5]);
    if (argc == 6 && !keep)
        return 2;
    const char *cases[] = {"real",
                           "compile-failure",
                           "capture",
                           "environment",
                           "mismatch",
                           "expected-crlf",
                           "invalid-utf8",
                           "missing-diagnostic",
                           "unexpected-success",
                           "unexpected-failure",
                           "invalid-path"};
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(*cases); ++i)
        if (!strcmp(argv[1], cases[i]))
            break;
    if (i == sizeof(cases) / sizeof(*cases))
        return 2;
    if (i == 3) {
#ifdef _WIN32
        if (_putenv_s("GOAT_MODULE_PATH", "parent module path")
            || _putenv_s("GOAT_HELPER_INHERITED", "retained value"))
            return 2;
#else
        if (setenv("GOAT_MODULE_PATH", "parent module path", 1)
            || setenv("GOAT_HELPER_INHERITED", "retained value", 1))
            return 2;
#endif
    }
    fs_error_t error = {0};
    char *cwd_before = absolute_fs_path(".", &error);
    clear_fs_error(&error);
    if (!cwd_before)
        return 2;
    state_t state = {argv[1], argv[4], argv[0], false, NULL};
    scenario_options_t options = {argv[1], argv[2], argv[3], keep, stderr};
    bool result = scenario_execute(&options, 60000, body, &state);
    bool expected = i < 4;
    char *cwd_after = absolute_fs_path(".", &error);
    clear_fs_error(&error);
    bool unchanged = cwd_after && !strcmp(cwd_before, cwd_after);
    FREE(cwd_before);
    FREE(cwd_after);
    if (i == 3)
        unchanged = unchanged && getenv("GOAT_MODULE_PATH")
                    && !strcmp(getenv("GOAT_MODULE_PATH"), "parent module path");
    bool diagnostic = true;
    if (!expected) {
        const char *reasons[] = {"byte mismatch",
                                 "byte mismatch",
                                 "module stdout is not UTF-8",
                                 "missing module diagnostic",
                                 "unexpected module command exit code",
                                 "unexpected module command exit code",
                                 "invalid relative fixture path"};
        char *log = state.work ? join_fs_path(state.work, "execution.log", &error) : NULL;
        void *data = NULL;
        size_t size = 0;
        diagnostic = log && read_fs_regular_file(log, 1000000, &data, &size, &error)
                     && strstr(data, reasons[i - 4]) != NULL;
        FREE(log);
        FREE(data);
        clear_fs_error(&error);
    }
    free(state.work);
    return result == expected && state.reached == expected && unchanged && diagnostic ? 0 : 1;
}
