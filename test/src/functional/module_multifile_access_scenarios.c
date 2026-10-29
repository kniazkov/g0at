/** @file module_multifile_access_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief AC-01--AC-28 across real includes/imports, host VM and public CLI (#359).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/module_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <stdlib.h>
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

static const char *field(scenario_run_t *r, yyjson_val *object, const char *key) {
    yyjson_val *v = yyjson_obj_get(object, key);
    const char *text = yyjson_get_str(v);
    REQUIRE(text && strlen(text) == yyjson_get_len(v));
    return text;
}

static bool relative(const char *path) {
    if (!path || !*path)
        return false;
    const char *start = path;
    for (const char *p = path;; p++) {
        if (*p == '\\' || *p == ':')
            return false;
        if (*p == '/' || !*p) {
            size_t n = (size_t)(p - start);
            if (!n || (n == 1 && *start == '.') || (n == 2 && start[0] == '.' && start[1] == '.'))
                return false;
            if (!*p)
                return true;
            start = p + 1;
        }
    }
}

static const char *path(scenario_run_t *r, const char *root, const char *name) {
    REQUIRE(relative(name));
    fs_error_t e = {0};
    char *p = join_fs_path(root, name, &e);
    clear_fs_error(&e);
    REQUIRE(p != NULL);
    scenario_defer(r, release, p);
    return p;
}

static void parents(scenario_run_t *r, const char *root, const char *name) {
    REQUIRE(relative(name));
    char *part = ALLOC(strlen(name) + 1);
    strcpy(part, name);
    scenario_defer(r, release, part);
    for (char *p = part; *p; p++) {
        if (*p != '/')
            continue;
        *p = 0;
        const char *directory = path(r, root, part);
        fs_error_t e = {0};
        bool ok = create_fs_directory(directory, &e);
        if (!ok && e.status == FS_EXISTS) {
            fs_info_t info;
            clear_fs_error(&e);
            ok = inspect_fs_path(directory, false, &info, &e) && info.kind == FS_DIRECTORY;
        }
        clear_fs_error(&e);
        REQUIRE(ok);
        *p = '/';
    }
}

static bool contains(const void *bytes, size_t size, const char *text) {
    size_t n = strlen(text);
    if (n > size)
        return false;
    for (size_t i = 0; i <= size - n; i++)
        if (!memcmp((const char *)bytes + i, text, n))
            return true;
    return false;
}

static void body(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    yyjson_val *fixture = c->fixture;
    module_execution_suite_t *suite = module_execution_suite(r, c->host);
    module_execution_project_t *project =
        module_execution_project(suite, field(r, fixture, "name"));
    const char *root = module_execution_root(project);
    yyjson_val *files = yyjson_obj_get(fixture, "files");
    REQUIRE(yyjson_is_obj(files) && yyjson_obj_size(files) > 0);
    size_t index, count;
    yyjson_val *key, *value;
    yyjson_obj_foreach(files, index, count, key, value) {
        const char *name = yyjson_get_str(key), *text = yyjson_get_str(value);
        REQUIRE(name && strlen(name) == yyjson_get_len(key));
        REQUIRE(text && strlen(text) == yyjson_get_len(value));
        module_execution_put(project, name, text);
    }
    yyjson_val *operations = yyjson_obj_get(fixture, "operations");
    REQUIRE(yyjson_is_arr(operations) && yyjson_arr_size(operations) > 0);
    size_t cli_count = 0;
    yyjson_arr_foreach(operations, index, count, value) {
        const char *mode = field(r, value, "mode");
        bool binary = !strcmp(mode, "binary");
        REQUIRE(binary || !strcmp(mode, "compile"));
        const char *input = path(r, root, field(r, value, "path"));
        const char *expected = field(r, value, "expected");
        const char *diagnostic = field(r, value, "diagnostic");
        yyjson_val *failure = yyjson_obj_get(value, "fail");
        REQUIRE(yyjson_is_bool(failure));
        bool fail = yyjson_get_bool(failure);
        const char *output = NULL;
        if (!binary) {
            const char *name = field(r, value, "output");
            parents(r, root, name);
            output = path(r, root, name);
        } else {
            REQUIRE(!fail && yyjson_is_null(yyjson_obj_get(value, "output")));
        }
        scenario_command_result_t host =
            module_execution_invoke(project, mode, input, output, expected, strlen(expected), fail);
        if (fail) {
            REQUIRE(*diagnostic && contains(host.err, host.err_size, diagnostic));
            fs_error_t e = {0};
            fs_info_t info;
            bool exists = inspect_fs_path(output, false, &info, &e);
            bool absent = !exists && e.status == FS_NOT_FOUND;
            clear_fs_error(&e);
            REQUIRE(absent);
        }
        if (!binary && !fail)
            continue;
        /* Repeat discovery from source and execution of the persisted binary,
         * from an unrelated cwd, with native disabled and both optimizers.
         * Proven compile-time denials must not become catchable VM failures. */
        const char *sources[] = {binary ? path(r, root, "sources/app-1/module.gmod") : input,
                                 input};
        const char *optimizers[] = {"none", "all"};
        for (size_t source = 0; source < (binary ? 2U : 1U); source++) {
            for (size_t optimization = 0; optimization < 2; optimization++) {
                const char *args[] = {c->goat,
                                      "--native",
                                      "off",
                                      "--optimize",
                                      optimizers[optimization],
                                      sources[source],
                                      NULL};
                child_options_t options = {.executable = c->goat,
                                           .arguments = args,
                                           .working_directory = module_execution_unrelated(suite),
                                           .environment = module_execution_environment(project)};
                scenario_command_result_t actual = scenario_command_capture(r, &options);
                REQUIRE(fail ? actual.exit_code != 0 : actual.exit_code == 0);
                scenario_require_bytes(r,
                                       "public CLI stdout",
                                       expected,
                                       strlen(expected),
                                       actual.out,
                                       actual.out_size,
                                       true);
                if (fail) {
                    REQUIRE(actual.err_size > 0);
                    const char *diagnostics[] = {"Access denied: read of x",
                                                 "Exactly one access modifier",
                                                 "Expected name with value assignment"};
                    for (size_t j = 0; j < 3; j++)
                        if (contains(host.err, host.err_size, diagnostics[j]))
                            REQUIRE(contains(actual.err, actual.err_size, diagnostics[j]));
                }
                cli_count++;
            }
        }
    }
    printf("%s: %zu compile/VM checks, %zu public CLI checks passed\n",
           field(r, fixture, "name"),
           module_execution_count(suite),
           cli_count);
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
        char *fixtures = join_fs_path(argv[6], "module-access/scenarios.json", &e);
        clear_fs_error(&e);
        yyjson_read_err error = {0};
        yyjson_doc *doc = fixtures ? yyjson_read_file(fixtures, 0, NULL, &error) : NULL;
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
                    "Cannot load access fixtures: %s\n",
                    error.msg ? error.msg : "invalid input");
        yyjson_doc_free(doc);
        FREE(fixtures);
        FREE(goat);
        FREE(host);
    } else
        fputs("usage: module_multifile_access_scenarios ID HOST GOAT BUILD DATA FIXTURES "
              "[--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
