/** @file native_operator_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Native operator families through source and compiler-less persisted execution.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *exe, *fixture;
    unsigned long long bound, attempts, successes;
} state_t;

#define COMMAND(...)                                                                               \
    language_command(run,                                                                          \
                     state->exe,                                                                   \
                     (const char *const[]){state->exe, __VA_ARGS__, NULL},                         \
                     environment)
#define CHECK(result) language_check(run, result, 0, expected, expected_size, "", 0)

static unsigned long long report_count(scenario_run_t *run, const char *report, const char *name) {
    size_t n = strlen(name);
    size_t found = 0;
    unsigned long long value = 0;
    for (const char *p = report; *p;) {
        if (!strncmp(p, name, n) && p[n] == '=') {
            char *end;
            value = strtoull(p + n + 1, &end, 10);
            scenario_require(run,
                             end != p + n + 1 && (*end == '\n' || *end == '\r' || !*end),
                             "invalid native report count");
            found++;
        }
        p = strchr(p, '\n');
        if (!p)
            break;
        p++;
    }
    scenario_require(run, found == 1, "missing or repeated native report count");
    return value;
}

static void native_report(scenario_run_t *run, const char *path, const state_t *state) {
    size_t size;
    const char *report = language_read(run, path, &size);
    scenario_require(run,
                     report_count(run, report, "bound") == state->bound,
                     "no native functions bound");
    scenario_require(run,
                     report_count(run, report, "attempts") == state->attempts,
                     "native code never entered");
    scenario_require(run,
                     report_count(run, report, "succeeded") == state->successes,
                     "native code never completed");
    scenario_require(run, report_count(run, report, "retries") == 0, "operator effects replayed");
}

static void body(scenario_run_t *run, void *opaque) {
    const state_t *state = opaque;
    const char *data =
        language_join(run, scenario_data_directory(scenario_run_context(run)), "native-operators");
    char name[96];
    snprintf(name, sizeof(name), "%s.goat", state->fixture);
    size_t code_size, expected_size;
    const char *code = language_read(run, language_join(run, data, name), &code_size);
    snprintf(name, sizeof(name), "%s.txt", state->fixture);
    const char *expected = language_read(run, language_join(run, data, name), &expected_size);
    const char *work = scenario_work_directory(scenario_run_context(run));
    const char *source = language_join(run, work, "program.goat");
    const char *binary = language_join(run, work, "program.gbin");
    const char *report = language_join(run, work, "native.txt");
    const char *const *environment = NULL;
    language_write(run, source, code, code_size);
    for (size_t optimization = 0; optimization < 2; optimization++) {
        CHECK(COMMAND("--lang",
                      "en",
                      "--native",
                      "off",
                      "--optimize",
                      optimization ? "all" : "none",
                      source));
    }
    for (size_t mode = 0; mode < 2; mode++) {
        CHECK(COMMAND("--lang",
                      "en",
                      "--native",
                      mode ? "required" : "auto",
                      "--save-native",
                      report,
                      source));
        native_report(run, report, state);
    }
    language_check(run,
                   COMMAND("--lang", "en", "--compile", "--native", "required", source),
                   0,
                   "",
                   0,
                   "",
                   0);
    language_remove(run, source);
    environment = language_environment(run, true);
    CHECK(COMMAND("--lang", "en", "--run", "--native", "off", binary));
    for (size_t mode = 0; mode < 2; mode++) {
        CHECK(COMMAND("--lang",
                      "en",
                      "--run",
                      "--native",
                      mode ? "required" : "auto",
                      "--save-native",
                      report,
                      binary));
        native_report(run, report, state);
    }
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t argument_error = {0};
    if (!fs_arguments(&argc, &argv, &argument_error)) {
        clear_fs_error(&argument_error);
        return 1;
    }
#endif
    int result = 1;
    static const char *names[] = {"arithmetic", "comparison", "logical", "index", "call", "unwind"};
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        for (size_t i = 0; i < 6; i++)
            printf("native.operators.%s\n", names[i]);
        result = 0;
    } else if (argc == 5 || argc == 6) {
        fs_error_t error = {0};
        char *exe = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        if (exe) {
            state_t states[6];
            scenario_definition_t definitions[6];
            char ids[6][80];
            for (size_t i = 0; i < 6; i++) {
                snprintf(ids[i], sizeof(ids[i]), "native.operators.%s", names[i]);
                static const unsigned long long counts[6][3] =
                    {{3, 14, 14}, {2, 4, 4}, {6, 5, 5}, {7, 16, 16}, {6, 7, 7}, {4, 12, 11}};
                states[i] = (state_t){exe, names[i], counts[i][0], counts[i][1], counts[i][2]};
                definitions[i] = (scenario_definition_t){ids[i], 180000, body, &states[i], NULL};
            }
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 6 ? argv[5] : NULL};
            result = scenario_registry_main(definitions, 6, argc == 6 ? 2 : 1, selection, &options);
            FREE(exe);
        }
    } else {
        fputs("usage: native_operator_testing CASE GOAT BUILD DATA [--keep-work]\n", stderr);
    }
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
