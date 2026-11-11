/** @file module_operator_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Cross-module operators in source and source/compiler-less saved processes.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(x) scenario_require(run, (x), #x)

typedef struct {
    const char *exe, *name, *optimization;
    unsigned long long bound, attempts, successes;
} state_t;

static const char *put(scenario_run_t *run, const char *dir, const char *name, const char *text) {
    const char *path = language_join(run, dir, name);
    language_write(run, path, text, strlen(text));
    return path;
}

static const char *
copy(scenario_run_t *run, const char *data, const char *work, const char *from, const char *to) {
    size_t size;
    const char *text = language_read(run, language_join(run, data, from), &size);
    const char *path = language_join(run, work, to);
    language_write(run, path, text, size);
    return path;
}

static unsigned long long count(scenario_run_t *run, const char *report, const char *name) {
    size_t n = strlen(name), found = 0;
    unsigned long long value = 0;
    for (const char *p = report; p && *p; p = strchr(p, '\n'), p = p ? p + 1 : NULL) {
        if (!strncmp(p, name, n) && p[n] == '=') {
            char *end;
            value = strtoull(p + n + 1, &end, 10);
            REQUIRE(end != p + n + 1 && (*end == '\n' || *end == '\r' || !*end));
            found++;
        }
    }
    REQUIRE(found == 1);
    return value;
}

static void native_report(scenario_run_t *run, const char *path, const state_t *state) {
    size_t size;
    const char *report = language_read(run, path, &size);
    REQUIRE(count(run, report, "bound") == state->bound);
    REQUIRE(count(run, report, "attempts") == state->attempts);
    REQUIRE(count(run, report, "succeeded") == state->successes);
    REQUIRE(count(run, report, "retries") == 0);
}

static scenario_command_result_t command(scenario_run_t *run,
                                         const state_t *state,
                                         const char *operation,
                                         const char *path,
                                         const char *modules,
                                         const char *mode,
                                         const char *report,
                                         const char *const *environment) {
    const char *arguments[] = {state->exe,
                               operation,
                               path,
                               "--module-dir",
                               modules,
                               "--native",
                               mode,
                               "--lang",
                               "en",
                               "--save-native",
                               report,
                               environment ? "--cc" : "--optimize",
                               environment ? "goat-missing-compiler-for-test" : state->optimization,
                               NULL};
    return language_command(run, state->exe, arguments, environment);
}

static void build(scenario_run_t *run,
                  const state_t *state,
                  const char *path,
                  const char *modules,
                  const char *report,
                  const char *mode) {
    language_check(run,
                   command(run, state, "build", path, modules, mode, report, NULL),
                   0,
                   "",
                   0,
                   "",
                   0);
}

static void execute(scenario_run_t *run,
                    const state_t *state,
                    const char *path,
                    const char *modules,
                    const char *report,
                    const char *mode,
                    const char *expected,
                    size_t size,
                    const char *const *environment) {
    bool trace = !strcmp(state->name, "trace");
    scenario_command_result_t result =
        command(run, state, "run", path, modules, mode, report, environment);
    language_check(run,
                   result,
                   trace ? 1 : 0,
                   trace ? "" : expected,
                   trace ? 0 : size,
                   trace ? expected : "",
                   trace ? size : 0);
    if (strcmp(mode, "off"))
        native_report(run, report, state);
}

static void body(scenario_run_t *run, void *opaque) {
    const state_t *state = opaque;
    const char *data =
        language_join(run, scenario_data_directory(scenario_run_context(run)), "module-operators");
    const char *work = scenario_work_directory(scenario_run_context(run));
    const char *modules = language_join(run, work, "modules");
    language_directory(run, modules);
    const char *report = language_join(run, work, "native.txt");
    const char *handlers = put(run,
                               work,
                               "handlers.gmod",
                               "{\"formatVersion\":1,\"name\":\"operator_handlers\",\"version\":"
                               "\"1\",\"main\":\"handlers.goat\"}");
    const char *receivers = put(run,
                                work,
                                "receivers.gmod",
                                "{\"formatVersion\":1,\"name\":\"operator_receivers\",\"version\":"
                                "\"1\",\"main\":\"receivers.goat\","
                                "\"dependencies\":{\"operator_handlers\":\"1\"}}");
    const char *failed = put(run,
                             work,
                             "failed.gmod",
                             "{\"formatVersion\":1,\"name\":\"operator_failed\",\"version\":\"1\","
                             "\"main\":\"failed.goat\","
                             "\"dependencies\":{\"operator_handlers\":\"1\"}}");
    bool semantics = !strcmp(state->name, "semantics");
    bool initializer = !strcmp(state->name, "initializer");
    const char *descriptor = put(
        run,
        work,
        "app.gmod",
        semantics     ? "{\"formatVersion\":1,\"name\":\"operator_app\",\"version\":\"1\",\"main\":"
                        "\"app.goat\","
                        "\"dependencies\":{\"operator_handlers\":\"1\",\"operator_receivers\":\"1\"}}"
        : initializer ? "{\"formatVersion\":1,\"name\":\"operator_app\",\"version\":\"1\",\"main\":"
                        "\"app.goat\","
                        "\"dependencies\":{\"operator_handlers\":\"1\",\"operator_failed\":\"1\"}}"
                      : "{\"formatVersion\":1,\"name\":\"operator_app\",\"version\":\"1\",\"main\":"
                        "\"app.goat\","
                        "\"dependencies\":{\"operator_handlers\":\"1\"}}");
    const char *sources[5];
    sources[0] = copy(run, data, work, "handlers.goat", "handlers.goat");
    sources[1] = copy(run, data, work, "implementation.goat", "implementation.goat");
    sources[2] = copy(run, data, work, "receivers.goat", "receivers.goat");
    sources[3] = copy(run, data, work, "failed.goat", "failed.goat");
    char name[80];
    snprintf(name, sizeof(name), "%s.goat", state->name);
    sources[4] = copy(run, data, work, name, "app.goat");
    snprintf(name, sizeof(name), "%s.txt", state->name);
    size_t size;
    const char *expected = language_read(run, language_join(run, data, name), &size);
    build(run, state, handlers, modules, report, "required");
    if (semantics)
        build(run, state, receivers, modules, report, "required");
    if (initializer)
        build(run, state, failed, modules, report, "auto");
    const char *modes[] = {"off", "auto", "required"};
    for (size_t i = 0; i < 3; i++)
        execute(run, state, descriptor, modules, report, modes[i], expected, size, NULL);
    const char *binary = language_join(run, modules, "operator_app/1.gbin");
    /* Bytecode-only producer artifacts are deterministic before native additions. */
    build(run, state, descriptor, modules, report, "off");
    size_t first_size, second_size;
    const char *first = language_read(run, binary, &first_size);
    build(run, state, descriptor, modules, report, "off");
    const char *second = language_read(run, binary, &second_size);
    scenario_require_bytes(run,
                           "deterministic operator archive",
                           first,
                           first_size,
                           second,
                           second_size,
                           false);
    build(run, state, descriptor, modules, report, "required");
    const char *archives[] = {binary,
                              language_join(run, modules, "operator_handlers/1.gbin"),
                              semantics ? language_join(run, modules, "operator_receivers/1.gbin")
                              : initializer ? language_join(run, modules, "operator_failed/1.gbin")
                                            : NULL};
    const char *archive_bytes[3];
    size_t archive_sizes[3], archive_count = semantics || initializer ? 3 : 2;
    for (size_t i = 0; i < archive_count; i++)
        archive_bytes[i] = language_read(run, archives[i], &archive_sizes[i]);
    for (size_t i = 0; i < 5; i++) {
        language_remove(run, sources[i]);
        REQUIRE(!language_exists(run, sources[i]));
    }
    const char *descriptors[] = {handlers, receivers, failed, descriptor};
    for (size_t i = 0; i < 4; i++)
        language_remove(run, descriptors[i]);
    const char *empty = language_join(run, work, "no-tools");
    language_directory(run, empty);
    const char *const *environment = language_environment_override(run, "PATH", empty);
    /* Absolute executable and absent source/compiler inputs: each call is a new process. */
    for (size_t i = 0; i < 3; i++)
        execute(run, state, binary, modules, report, modes[i], expected, size, environment);
    for (size_t i = 0; i < archive_count; i++) {
        size_t after_size;
        const char *after = language_read(run, archives[i], &after_size);
        scenario_require_bytes(run,
                               "saved operator archive unchanged",
                               archive_bytes[i],
                               archive_sizes[i],
                               after,
                               after_size,
                               false);
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
    static const char *names[] = {"semantics", "trace", "initializer"};
    static const unsigned long long counts[][3] = {{4, 6, 6}, {3, 1, 0}, {3, 1, 1}};
    int result = 1;
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        for (size_t i = 0; i < 3; i++)
            for (size_t j = 0; j < 2; j++)
                printf("module.operators.%s.%s\n", names[i], j ? "optimized" : "unoptimized");
        result = 0;
    } else if (argc == 5 || argc == 6) {
        fs_error_t error = {0};
        char *exe = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        if (exe) {
            state_t states[6];
            scenario_definition_t definitions[6];
            char ids[6][96];
            for (size_t i = 0; i < 6; i++) {
                size_t fixture = i / 2;
                snprintf(ids[i],
                         sizeof(ids[i]),
                         "module.operators.%s.%s",
                         names[fixture],
                         i % 2 ? "optimized" : "unoptimized");
                states[i] = (state_t){exe,
                                      names[fixture],
                                      i % 2 ? "all" : "none",
                                      counts[fixture][0],
                                      counts[fixture][1],
                                      counts[fixture][2]};
                definitions[i] = (scenario_definition_t){ids[i], 180000, body, &states[i], NULL};
            }
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 6 ? argv[5] : NULL};
            result = scenario_registry_main(definitions, 6, argc == 6 ? 2 : 1, selection, &options);
            FREE(exe);
        }
    } else {
        fputs("usage: module_operator_scenarios CASE GOAT BUILD DATA [--keep-work]\n", stderr);
    }
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
