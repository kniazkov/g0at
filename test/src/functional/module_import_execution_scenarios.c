/** @file module_import_execution_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real immutable import graphs, preflight and frozen execution (#362).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/module_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <stdlib.h>
#include <string.h>

#define REQUIRE(x) scenario_require(r, (x), #x)

typedef struct {
    const char *host, *schema;
    yyjson_val *fixture;
} configuration_t;

static const char *field(scenario_run_t *r, yyjson_val *object, const char *name) {
    yyjson_val *value = yyjson_obj_get(object, name);
    const char *text = yyjson_get_str(value);
    REQUIRE(text && strlen(text) == yyjson_get_len(value));
    return text;
}

static bool free_malloc(void *p) {
    free(p);
    return true;
}

static bool free_mut(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static void *owned(scenario_run_t *r, void *p) {
    REQUIRE(p != NULL);
    scenario_defer(r, free_malloc, p);
    return p;
}

static const char *join(scenario_run_t *r, const char *root, const char *relative) {
    REQUIRE(relative && *relative);
    const char *component = relative;
    for (const char *p = relative;; p++) {
        REQUIRE(*p != '\\' && *p != ':');
        if (*p == '/' || !*p) {
            size_t n = (size_t)(p - component);
            REQUIRE(n && !(n == 1 && *component == '.')
                    && !(n == 2 && component[0] == '.' && component[1] == '.'));
            if (!*p)
                break;
            component = p + 1;
        }
    }
    return language_join(r, root, relative);
}

static yyjson_mut_doc *mut(scenario_run_t *r, const void *bytes, size_t size) {
    char error[256] = {0};
    yyjson_mut_doc *d = format_json_parse(bytes, size, error, sizeof(error));
    scenario_require(r, d != NULL, error);
    scenario_defer(r, free_mut, d);
    return d;
}

/* Independently inspect/extract ZIP members; never ask the production validator
 * to calculate an expected metadata or identity value. */
typedef struct {
    format_zip_entry_t *entries;
    size_t count, metadata, bytecode;
} archive_t;

static archive_t archive(scenario_run_t *r, const char *path) {
    size_t size;
    const unsigned char *raw = (const unsigned char *)language_read(r, path, &size);
    format_zip_info_t info;
    char error[256] = {0};
    REQUIRE(format_zip_inspect(raw, size, SIZE_MAX, &info, error, sizeof(error)));
    archive_t a = {0};
    a.count = info.count;
    a.metadata = a.bytecode = SIZE_MAX;
    a.entries = owned(r, calloc(a.count, sizeof(*a.entries)));
    for (size_t i = 0; i < a.count; i++) {
        REQUIRE(format_zip_inspect(raw, size, i, &info, error, sizeof(error)));
        char *name = owned(r, malloc(info.name_size + 1));
        memcpy(name, info.name, info.name_size);
        name[info.name_size] = 0;
        REQUIRE(strlen(name) == info.name_size);
        size_t n;
        void *bytes =
            owned(r, format_zip_extract(raw, size, i, 16 * 1024 * 1024, &n, error, sizeof(error)));
        format_zip_entry_init(a.entries + i, name, bytes, n, false);
        if (!strcmp(name, "module.json"))
            a.metadata = i;
        if (!strcmp(name, "bytecode.bin"))
            a.bytecode = i;
    }
    REQUIRE(a.metadata != SIZE_MAX && a.bytecode != SIZE_MAX);
    return a;
}

static void nonzero(scenario_run_t *r, configuration_t *c, const char *path) {
    archive_t a = archive(r, path);
    format_zip_entry_t *meta = a.entries + a.metadata;
    yyjson_mut_doc *d = mut(r, meta->data, meta->data_size);
    yyjson_mut_val *v = yyjson_mut_doc_get_root(d);
    REQUIRE(yyjson_mut_obj_put(v, yyjson_mut_str(d, "main"), yyjson_mut_uint(d, 1)));
    yyjson_mut_val *externals = yyjson_mut_obj(d), *value = yyjson_mut_obj(d);
    REQUIRE(yyjson_mut_obj_add_bool(d, value, "mutable", false));
    REQUIRE(yyjson_mut_obj_add_str(d, value, "type", "integer"));
    REQUIRE(yyjson_mut_obj_add_int(d, value, "value", 42));
    REQUIRE(yyjson_mut_obj_add_val(d, externals, "value", value));
    REQUIRE(yyjson_mut_obj_put(v, yyjson_mut_str(d, "externals"), externals));
    size_t size;
    const char *schema = language_read(r, c->schema, &size);
    yyjson_mut_doc *source = mut(r, schema, size);
    char hash[65], error[256] = {0};
    format_zip_entry_t *code = a.entries + a.bytecode;
    REQUIRE(format_binary_main_hash(v,
                                    yyjson_mut_doc_get_root(source),
                                    code->data,
                                    code->data_size,
                                    hash,
                                    error,
                                    sizeof(error)));
    REQUIRE(yyjson_mut_obj_put(v, yyjson_mut_str(d, "mainHash"), yyjson_mut_strcpy(d, hash)));
    char *json = owned(r, yyjson_mut_write(d, 0, &size));
    format_zip_entry_init(meta, "module.json", json, size, false);
    unsigned char *zip =
        owned(r, format_zip_build(a.entries, a.count, NULL, 0, false, &size, error, sizeof(error)));
    language_write(r, path, zip, size);
}

static void dependencies(scenario_run_t *r, const char *path, yyjson_val *expected) {
    archive_t a = archive(r, path);
    format_zip_entry_t *m = a.entries + a.metadata;
    yyjson_doc *d = yyjson_read((const char *)m->data, m->data_size, 0);
    REQUIRE(d);
    scenario_defer(r, free_doc, d);
    yyjson_val *actual = yyjson_obj_get(yyjson_doc_get_root(d), "dependencies");
    REQUIRE(yyjson_is_arr(expected) && yyjson_is_arr(actual)
            && yyjson_arr_size(actual) == yyjson_arr_size(expected));
    for (size_t i = 0; i < yyjson_arr_size(expected); i++) {
        yyjson_val *row = yyjson_arr_get(expected, i), *entry = yyjson_arr_get(actual, i);
        REQUIRE(yyjson_is_arr(row) && yyjson_arr_size(row) >= 1 && yyjson_arr_size(row) <= 2);
        const char *keys[] = {"name", "version"};
        for (size_t j = 0; j < yyjson_arr_size(row); j++) {
            const char *text = yyjson_get_str(yyjson_arr_get(row, j));
            REQUIRE(text);
            REQUIRE(!strcmp(field(r, entry, keys[j]), text));
        }
    }
}

static void delete_project(scenario_run_t *r, void *arg) {
    fs_error_t e = {0};
    bool ok = remove_fs_tree(arg, &e);
    clear_fs_error(&e);
    REQUIRE(ok);
}

static void body(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    module_execution_suite_t *suite = module_execution_suite(r, c->host);
    module_execution_project_t *project =
        module_execution_project(suite, field(r, c->fixture, "name"));
    const char *root = module_execution_root(project);
    yyjson_val *operations = yyjson_obj_get(c->fixture, "operations");
    REQUIRE(yyjson_is_arr(operations) && yyjson_arr_size(operations) > 0);
    size_t index, count, barriers = 0;
    yyjson_val *operation;
    yyjson_arr_foreach(operations, index, count, operation) {
        const char *mode = field(r, operation, "mode");
        if (!strcmp(mode, "compile")) {
            yyjson_val *values = yyjson_obj_get(operation, "files");
            REQUIRE(yyjson_is_obj(values));
            size_t n = yyjson_obj_size(values), i, total;
            yyjson_val *key, *value;
            module_execution_file_t *files = language_owned(r, CALLOC((n + 1) * sizeof(*files)));
            yyjson_obj_foreach(values, i, total, key, value) {
                const char *name = yyjson_get_str(key), *text = yyjson_get_str(value);
                REQUIRE(name && strlen(name) == yyjson_get_len(key));
                REQUIRE(text && strlen(text) == yyjson_get_len(value));
                files[i] = (module_execution_file_t){name, text};
            }
            yyjson_val *failure = yyjson_obj_get(operation, "fail"),
                       *deps = yyjson_obj_get(operation, "dependencies");
            REQUIRE(yyjson_is_bool(failure) && (yyjson_is_null(deps) || yyjson_is_obj(deps)));
            char *json = yyjson_is_null(deps) ? NULL : owned(r, yyjson_val_write(deps, 0, NULL));
            module_execution_compile(project,
                                     field(r, operation, "name"),
                                     field(r, operation, "code"),
                                     field(r, operation, "version"),
                                     files,
                                     n,
                                     json,
                                     yyjson_get_bool(failure));
        } else if (!strcmp(mode, "write")) {
            module_execution_put(project, field(r, operation, "path"), field(r, operation, "text"));
        } else if (!strcmp(mode, "remove")) {
            language_remove(r, join(r, root, field(r, operation, "path")));
        } else if (!strcmp(mode, "nonzero-main")) {
            nonzero(r, c, join(r, root, field(r, operation, "path")));
        } else if (!strcmp(mode, "dependencies")) {
            dependencies(r,
                         join(r, root, field(r, operation, "path")),
                         yyjson_obj_get(operation, "expected"));
        } else if (!strcmp(mode, "absent")) {
            yyjson_val *paths = yyjson_obj_get(operation, "paths");
            REQUIRE(yyjson_is_arr(paths) && yyjson_arr_size(paths) > 0);
            for (size_t i = 0; i < yyjson_arr_size(paths); i++) {
                const char *name = yyjson_get_str(yyjson_arr_get(paths, i));
                REQUIRE(name);
                fs_error_t error = {0};
                fs_info_t info;
                bool exists = inspect_fs_path(join(r, root, name), false, &info, &error);
                REQUIRE(!exists && error.status == FS_NOT_FOUND);
                clear_fs_error(&error);
            }
        } else if (!strcmp(mode, "invoke")) {
            const char *command = field(r, operation, "command");
            REQUIRE(!strcmp(command, "compile") || !strcmp(command, "binary"));
            const char *path = join(r, root, field(r, operation, "path"));
            yyjson_val *output = yyjson_obj_get(operation, "output");
            const char *out = NULL;
            if (yyjson_is_str(output))
                out = join(r, root, field(r, operation, "output"));
            else
                REQUIRE(yyjson_is_null(output));
            REQUIRE(!strcmp(command, "compile") ? out != NULL : out == NULL);
            yyjson_val *fail = yyjson_obj_get(operation, "fail");
            REQUIRE(yyjson_is_bool(fail));
            const char *expected = field(r, operation, "expected");
            module_execution_invoke(project,
                                    command,
                                    path,
                                    out,
                                    expected,
                                    strlen(expected),
                                    yyjson_get_bool(fail));
        } else {
            REQUIRE(!strcmp(mode, "frozen"));
            const char *args[] = {c->host,
                                  "frozen",
                                  join(r, root, field(r, operation, "path")),
                                  NULL};
            child_options_t options = {.executable = c->host,
                                       .arguments = args,
                                       .working_directory = module_execution_unrelated(suite),
                                       .environment = module_execution_environment(project)};
            scenario_command_result_t result = scenario_command_barrier(r,
                                                                        &options,
#ifdef _WIN32
                                                                        "READY\r\n",
#else
                                                                        "READY\n",
#endif
                                                                        delete_project,
                                                                        (void *)root);
            REQUIRE(result.exit_code == 0);
            const char *expected = field(r, operation, "expected");
            char *full = language_owned(r, ALLOC(strlen(expected) + 7));
            strcpy(full, "READY\n");
            strcat(full, expected);
            scenario_require_bytes(r,
                                   "frozen stdout",
                                   full,
                                   strlen(full),
                                   result.out,
                                   result.out_size,
                                   true);
            fs_error_t error = {0};
            fs_info_t info;
            bool exists = inspect_fs_path(root, false, &info, &error);
            REQUIRE(!exists && error.status == FS_NOT_FOUND);
            clear_fs_error(&error);
            barriers++;
        }
    }
    printf("%s: %zu compile/VM checks passed\n",
           field(r, c->fixture, "name"),
           module_execution_count(suite) + barriers);
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
        char *path = join_fs_path(argv[5], "module-import-execution/scenarios.json", &e);
        clear_fs_error(&e);
        void *data = NULL;
        size_t size = 0;
        bool read = path && read_fs_regular_file(path, 1024 * 1024, &data, &size, &e);
        clear_fs_error(&e);
        yyjson_read_err error = {0};
        yyjson_doc *doc = read ? yyjson_read_opts(data, size, 0, NULL, &error) : NULL;
        yyjson_val *array = yyjson_doc_get_root(doc);
        size_t count = yyjson_arr_size(array);
        if (host && yyjson_is_arr(array) && count > 0) {
            configuration_t *config = CALLOC(count * sizeof(*config));
            scenario_definition_t *defs = CALLOC(count * sizeof(*defs));
            for (size_t i = 0; i < count; i++) {
                yyjson_val *fixture = yyjson_arr_get(array, i);
                config[i] = (configuration_t){host, argv[6], fixture};
                defs[i] = (scenario_definition_t){yyjson_get_str(yyjson_obj_get(fixture, "id")),
                                                  120000,
                                                  body,
                                                  &config[i],
                                                  NULL};
            }
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 8 ? argv[7] : NULL};
            result = scenario_registry_main(defs, count, argc == 8 ? 2 : 1, selection, &options);
            FREE(defs);
            FREE(config);
        } else
            fprintf(stderr,
                    "Cannot load import execution fixtures: %s\n",
                    error.msg ? error.msg : "invalid input");
        yyjson_doc_free(doc);
        FREE(data);
        FREE(path);
        FREE(host);
    } else
        fputs("usage: module_import_execution_scenarios ID HOST BUILD DATA FIXTURES SCHEMA "
              "[--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
