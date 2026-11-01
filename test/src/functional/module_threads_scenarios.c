/** @file module_threads_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Production VM threads, initializer restrictions and shared lifetime (#378).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/module_execution.h"
#include "test/support/module_threads_special.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)

typedef struct {
    const char *host, *goat, *lifetime, *cwd;
    yyjson_val *fixture;
} config_t;

typedef struct {
    config_t *config;
    module_execution_suite_t *suite;
    module_execution_project_t *project;
    yyjson_val *step;
} step_t;

static const char *field(scenario_run_t *r, yyjson_val *object, const char *name) {
    yyjson_val *value = yyjson_obj_get(object, name);
    const char *text = yyjson_get_str(value);
    REQUIRE(text && strlen(text) == yyjson_get_len(value));
    return text;
}

static const char *argument(scenario_run_t *r, step_t *s, const char *token) {
    if (!strcmp(token, "$HOST"))
        return s->config->host;
    if (!strcmp(token, "$GOAT"))
        return s->config->goat;
    if (!strcmp(token, "$ROOT"))
        return module_execution_root(s->project);
    if (!strncmp(token, "$ROOT/", 6))
        return language_join(r, module_execution_root(s->project), token + 6);
    REQUIRE(token[0] != '$');
    return token;
}

static void execute_step(scenario_run_t *r, void *user) {
    step_t *s = user;
    yyjson_val *step = s->step;
    const char *kind = field(r, step, "kind");
    if (!strcmp(kind, "write")) {
        module_execution_put(s->project, field(r, step, "path"), field(r, step, "text"));
        return;
    }
    if (!strcmp(kind, "compile")) {
        yyjson_val *values = yyjson_obj_get(step, "files");
        REQUIRE(yyjson_is_obj(values));
        size_t n = yyjson_obj_size(values);
        module_execution_file_t *files = language_owned(r, CALLOC((n + 1) * sizeof(*files)));
        size_t index, count;
        yyjson_val *key, *value;
        yyjson_obj_foreach(values, index, count, key, value) {
            const char *name = yyjson_get_str(key), *text = yyjson_get_str(value);
            REQUIRE(name && strlen(name) == yyjson_get_len(key));
            REQUIRE(text && strlen(text) == yyjson_get_len(value));
            files[index] = (module_execution_file_t){name, text};
        }
        module_execution_compile(s->project,
                                 field(r, step, "name"),
                                 field(r, step, "code"),
                                 field(r, step, "version"),
                                 files,
                                 n,
                                 NULL,
                                 false);
        return;
    }
    if (!strcmp(kind, "unlink")) {
        const char *path =
            language_join(r, module_execution_root(s->project), field(r, step, "path"));
        fs_error_t error = {0};
        bool removed = remove_fs_entry(path, &error);
        bool missing = error.status == FS_NOT_FOUND;
        clear_fs_error(&error);
        REQUIRE(removed || missing);
        return;
    }
    if (!strcmp(kind, "lifetime")) {
        const char *spelling = field(r, step, "spelling");
        REQUIRE(!strcmp(spelling, "absolute") || !strcmp(spelling, "relative"));
        module_threads_lifetime(
            r,
            s->config->lifetime,
            language_join(r, module_execution_root(s->project), "lifetime путь😀/main.goat"),
            !strcmp(spelling, "relative"));
        return;
    }
    if (!strcmp(kind, "handshake")) {
        module_threads_handshake(r,
                                 s->config->host,
                                 language_join(r, module_execution_root(s->project), "app.gbin"),
                                 module_execution_unrelated(s->suite),
                                 module_execution_environment(s->project));
        return;
    }
    REQUIRE(!strcmp(kind, "invoke"));
    yyjson_val *args = yyjson_obj_get(step, "argv");
    REQUIRE(yyjson_is_arr(args) && yyjson_arr_size(args) > 0);
    size_t n = yyjson_arr_size(args);
    const char **argv = language_owned(r, CALLOC((n + 1) * sizeof(*argv)));
    for (size_t i = 0; i < n; i++) {
        yyjson_val *value = yyjson_arr_get(args, i);
        const char *text = yyjson_get_str(value);
        REQUIRE(text && strlen(text) == yyjson_get_len(value));
        argv[i] = argument(r, s, text);
    }
    const char *cwd = field(r, step, "cwd");
    REQUIRE(!strcmp(cwd, "inherit") || !strcmp(cwd, "unrelated"));
    child_options_t options = {.executable = argv[0],
                               .arguments = argv,
                               .working_directory = !strcmp(cwd, "inherit")
                                                        ? s->config->cwd
                                                        : module_execution_unrelated(s->suite),
                               .environment = module_execution_environment(s->project),
                               .empty_stdin = true};
    scenario_command_result_t result = scenario_command_capture(r, &options);
    yyjson_val *status = yyjson_obj_get(step, "status");
    REQUIRE(yyjson_is_int(status) && yyjson_get_sint(status) >= 0
            && yyjson_get_sint(status) <= 255);
    REQUIRE(result.exit_code == yyjson_get_sint(status));
    yyjson_val *expected_value = yyjson_obj_get(step, "expected");
    REQUIRE(yyjson_is_null(expected_value) || yyjson_is_str(expected_value));
    if (!yyjson_is_null(expected_value)) {
        const char *expected = field(r, step, "expected");
        scenario_require_bytes(r,
                               "module threads stdout",
                               expected,
                               strlen(expected),
                               result.out,
                               result.out_size,
                               true);
    }
    REQUIRE(!strcmp(field(r, step, "stderr_policy"), "ignore"));
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
    module_execution_suite_t *suite = module_execution_suite(r, c->host);
    module_execution_project_t *project =
        module_execution_project(suite, field(r, c->fixture, "name"));
    yyjson_val *steps = yyjson_obj_get(c->fixture, "steps");
    REQUIRE(yyjson_is_arr(steps) && yyjson_arr_size(steps) > 0);
    size_t index, count;
    yyjson_val *step;
    yyjson_arr_foreach(steps, index, count, step) {
        step_t call = {c, suite, project, step};
        scenario_with_timeout(r,
                              !strcmp(field(r, step, "kind"), "handshake") ? 180000 : 60000,
                              execute_step,
                              &call);
    }
    printf("%s: %zu ordered compile/entry-point steps passed\n",
           field(r, c->fixture, "name"),
           yyjson_arr_size(steps));
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
    if (argc == 8 || (argc == 9 && !strcmp(argv[8], "--keep-work"))) {
        fs_error_t e = {0};
        char *host = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        char *goat = absolute_fs_path(argv[3], &e);
        clear_fs_error(&e);
        char *lifetime = absolute_fs_path(argv[4], &e);
        clear_fs_error(&e);
        char *cwd = absolute_fs_path(".", &e);
        clear_fs_error(&e);
        char *path = join_fs_path(argv[7], "scenarios.json", &e);
        clear_fs_error(&e);
        void *data = NULL;
        size_t size = 0;
        bool read = path && read_fs_regular_file(path, 1024 * 1024, &data, &size, &e);
        clear_fs_error(&e);
        yyjson_read_err error = {0};
        yyjson_doc *doc = read ? yyjson_read_opts(data, size, 0, NULL, &error) : NULL;
        yyjson_val *array = yyjson_doc_get_root(doc);
        size_t count = yyjson_arr_size(array);
        if (host && goat && lifetime && cwd && yyjson_is_arr(array) && count > 0) {
            config_t *config = CALLOC(count * sizeof(*config));
            scenario_definition_t *defs = CALLOC(count * sizeof(*defs));
            for (size_t i = 0; i < count; i++) {
                yyjson_val *fixture = yyjson_arr_get(array, i);
                config[i] = (config_t){host, goat, lifetime, cwd, fixture};
                defs[i] = (scenario_definition_t){yyjson_get_str(yyjson_obj_get(fixture, "id")),
                                                  240000,
                                                  body,
                                                  config + i,
                                                  NULL};
            }
            scenario_options_t options = {.build_directory = argv[5], .data_directory = argv[6]};
            const char *selection[] = {argv[1], argc == 9 ? argv[8] : NULL};
            result = scenario_registry_main(defs, count, argc == 9 ? 2 : 1, selection, &options);
            FREE(defs);
            FREE(config);
        } else
            fprintf(stderr,
                    "Cannot load module-threads fixtures: %s\n",
                    error.msg ? error.msg : "invalid input");
        yyjson_doc_free(doc);
        FREE(data);
        FREE(path);
        FREE(host);
        FREE(goat);
        FREE(lifetime);
        FREE(cwd);
    } else
        fputs("usage: module_threads_scenarios ID HOST GOAT LIFETIME BUILD DATA FIXTURES "
              "[--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
