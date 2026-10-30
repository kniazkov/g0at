/** @file module_assignment_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Assignment access and exact evaluation-order rules 13--25 (#361).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/module_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <string.h>

#define REQUIRE(x) scenario_require(r, (x), #x)

typedef struct {
    const char *host, *goat;
    yyjson_val *fixture;
} configuration_t;

static bool release(void *p) {
    FREE(p);
    return true;
}

static const char *field(scenario_run_t *r, yyjson_val *object, const char *name) {
    yyjson_val *value = yyjson_obj_get(object, name);
    const char *text = yyjson_get_str(value);
    REQUIRE(text && strlen(text) == yyjson_get_len(value));
    return text;
}

static void body(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    module_execution_suite_t *suite = module_execution_suite(r, c->host);
    module_execution_project_t *project =
        module_execution_project(suite, field(r, c->fixture, "name"));
    const char *kind = field(r, c->fixture, "kind");
    if (!strcmp(kind, "cli")) {
        const char *source =
            module_execution_put(project, "cli.goat", field(r, c->fixture, "code"));
        const char *optimization = field(r, c->fixture, "optimization");
        REQUIRE(!strcmp(optimization, "none") || !strcmp(optimization, "all"));
        const char *args[] = {c->goat, "--optimize", optimization, source, NULL};
        /* Preserve the original CLI's inherited environment and default native
         * selection: do not replace these with the host's module environment. */
        child_options_t options = {.executable = c->goat,
                                   .arguments = args,
                                   .working_directory = module_execution_unrelated(suite)};
        scenario_command_result_t result = scenario_command_capture(r, &options);
        REQUIRE(result.exit_code == 0);
        const char *expected = field(r, c->fixture, "expected");
        scenario_require_bytes(r,
                               "assignment CLI stdout",
                               expected,
                               strlen(expected),
                               result.out,
                               result.out_size,
                               true);
        printf("%s (%s): 1 public CLI check passed\n", field(r, c->fixture, "name"), optimization);
        return;
    }
    REQUIRE(!strcmp(kind, "host"));
    yyjson_val *operations = yyjson_obj_get(c->fixture, "operations");
    REQUIRE(yyjson_is_arr(operations) && yyjson_arr_size(operations) > 0);
    const char *artifact = NULL;
    size_t index, count;
    yyjson_val *operation;
    yyjson_arr_foreach(operations, index, count, operation) {
        const char *mode = field(r, operation, "mode");
        if (!strcmp(mode, "compile")) {
            yyjson_val *file_values = yyjson_obj_get(operation, "files");
            REQUIRE(yyjson_is_obj(file_values));
            size_t n = yyjson_obj_size(file_values);
            module_execution_file_t *files = CALLOC((n + 1) * sizeof(*files));
            scenario_defer(r, release, files);
            size_t file_index, file_count;
            yyjson_val *key, *value;
            yyjson_obj_foreach(file_values, file_index, file_count, key, value) {
                const char *name = yyjson_get_str(key), *text = yyjson_get_str(value);
                REQUIRE(name && strlen(name) == yyjson_get_len(key));
                REQUIRE(text && strlen(text) == yyjson_get_len(value));
                files[file_index] = (module_execution_file_t){name, text};
            }
            yyjson_val *failure = yyjson_obj_get(operation, "fail");
            REQUIRE(yyjson_is_bool(failure));
            REQUIRE(yyjson_is_null(yyjson_obj_get(operation, "dependencies")));
            artifact = module_execution_compile(project,
                                                field(r, operation, "name"),
                                                field(r, operation, "code"),
                                                field(r, operation, "version"),
                                                files,
                                                n,
                                                NULL,
                                                yyjson_get_bool(failure));
            /* Failed compiles are already required to leave no artifact. */
            if (yyjson_get_bool(failure))
                artifact = NULL;
        } else {
            REQUIRE(!strcmp(mode, "binary") && artifact != NULL);
            const char *expected = field(r, operation, "expected");
            module_execution_invoke(project,
                                    "binary",
                                    artifact,
                                    NULL,
                                    expected,
                                    strlen(expected),
                                    false);
        }
    }
    printf("%s: %zu compile/VM checks passed\n",
           field(r, c->fixture, "name"),
           module_execution_count(suite));
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
    if (argc == 7 || argc == 8) {
        fs_error_t e = {0};
        char *host = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        char *goat = absolute_fs_path(argv[3], &e);
        clear_fs_error(&e);
        char *path = join_fs_path(argv[6], "module-assignment/scenarios.json", &e);
        clear_fs_error(&e);
        void *data = NULL;
        size_t size = 0;
        bool read = path && read_fs_regular_file(path, 1024 * 1024, &data, &size, &e);
        clear_fs_error(&e);
        yyjson_read_err error = {0};
        yyjson_doc *doc = read ? yyjson_read_opts(data, size, 0, NULL, &error) : NULL;
        yyjson_val *array = yyjson_doc_get_root(doc);
        size_t count = yyjson_arr_size(array);
        if (host && goat && yyjson_is_arr(array) && count > 0) {
            configuration_t *config = CALLOC(count * sizeof(*config));
            scenario_definition_t *defs = CALLOC(count * sizeof(*defs));
            for (size_t i = 0; i < count; i++) {
                yyjson_val *fixture = yyjson_arr_get(array, i);
                config[i] = (configuration_t){host, goat, fixture};
                defs[i] = (scenario_definition_t){yyjson_get_str(yyjson_obj_get(fixture, "id")),
                                                  120000,
                                                  body,
                                                  &config[i],
                                                  NULL};
            }
            scenario_options_t options = {.build_directory = argv[4], .data_directory = argv[5]};
            const char *selection[] = {argv[1], argc == 8 ? argv[7] : NULL};
            result = scenario_registry_main(defs, count, argc == 8 ? 2 : 1, selection, &options);
            FREE(defs);
            FREE(config);
        } else
            fprintf(stderr,
                    "Cannot load assignment fixtures: %s\n",
                    error.msg ? error.msg : "invalid input");
        yyjson_doc_free(doc);
        FREE(data);
        FREE(path);
        FREE(host);
        FREE(goat);
    } else
        fputs("usage: module_assignment_scenarios ID HOST GOAT BUILD DATA FIXTURES "
              "[--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
