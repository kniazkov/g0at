/** @file module_interface_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Producer interfaces and metadata-only consumer contracts (#362).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/module_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <stdlib.h>
#include <string.h>

#define REQUIRE(x) scenario_require(r, (x), #x)
#define N(x) (sizeof(x) / sizeof((x)[0]))

typedef struct {
    const char *host;
    yyjson_val *fixture;
} configuration_t;

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool free_malloc(void *p) {
    free(p);
    return true;
}

static const char *field(scenario_run_t *r, yyjson_val *obj, const char *key) {
    yyjson_val *v = yyjson_obj_get(obj, key);
    const char *s = yyjson_get_str(v);
    REQUIRE(s && strlen(s) == yyjson_get_len(v));
    return s;
}

static bool relative(const char *s) {
    if (!s || !*s)
        return false;
    const char *start = s;
    for (const char *p = s;; p++) {
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

static const char *path(scenario_run_t *r, module_execution_project_t *p, const char *name) {
    REQUIRE(relative(name));
    return language_join(r, module_execution_root(p), name);
}

static void equal(scenario_run_t *r, yyjson_val *actual, const char *expected) {
    yyjson_doc *d = yyjson_read(expected, strlen(expected), 0);
    REQUIRE(d != NULL);
    scenario_defer(r, free_doc, d);
    REQUIRE(actual && yyjson_equals(actual, yyjson_doc_get_root(d)));
}

static yyjson_val *function(scenario_run_t *r, yyjson_val *doc, uint64_t id) {
    yyjson_val *a = yyjson_obj_get(doc, "functions"), *v;
    REQUIRE(yyjson_is_arr(a));
    size_t i, n;
    yyjson_arr_foreach(a, i, n, v) {
        yyjson_val *value = yyjson_obj_get(v, "functionId");
        if (yyjson_is_uint(value) && yyjson_get_uint(value) == id)
            return v;
    }
    REQUIRE(false);
    return NULL;
}

static yyjson_val *export_function(scenario_run_t *r, yyjson_val *doc, const char *name) {
    yyjson_val *fields = yyjson_obj_get(doc, "externals");
    yyjson_val *id = yyjson_obj_get(yyjson_obj_get(fields, name), "functionId");
    REQUIRE(yyjson_is_uint(id));
    return function(r, doc, yyjson_get_uint(id));
}

static yyjson_val *summaries(scenario_run_t *r, yyjson_val *record, bool required) {
    yyjson_val *a = yyjson_obj_get(yyjson_obj_get(record, "analysis"), "summaries");
    REQUIRE(!a || yyjson_is_arr(a));
    if (required)
        REQUIRE(a && yyjson_arr_size(a));
    return a;
}

static yyjson_val *metadata(scenario_run_t *r, const char *filename) {
    size_t size;
    const unsigned char *bytes = (const unsigned char *)language_read(r, filename, &size);
    format_zip_info_t info;
    char error[256];
    REQUIRE(format_zip_inspect(bytes, size, SIZE_MAX, &info, error, sizeof(error)));
    size_t count = info.count;
    for (size_t i = 0; i < count; i++) {
        REQUIRE(format_zip_inspect(bytes, size, i, &info, error, sizeof(error)));
        if (info.name_size != 11 || memcmp(info.name, "module.json", 11))
            continue;
        size_t n;
        unsigned char *json =
            format_zip_extract(bytes, size, i, 16 * 1024 * 1024, &n, error, sizeof(error));
        REQUIRE(json != NULL);
        scenario_defer(r, free_malloc, json);
        yyjson_doc *d = yyjson_read((const char *)json, n, 0);
        REQUIRE(d != NULL);
        scenario_defer(r, free_doc, d);
        yyjson_val *root = yyjson_doc_get_root(d);
        REQUIRE(yyjson_is_obj(root));
        return root;
    }
    REQUIRE(false);
    return NULL;
}

static void
check_metadata(scenario_run_t *r, const char *name, const char *filename, yyjson_val *doc) {
    yyjson_val *fields = yyjson_obj_get(doc, "externals");
    REQUIRE(yyjson_is_obj(fields));
    bool app = !strcmp(filename, "app.gbin");
    if (!strcmp(name, "scalars")) {
        const char *keys[] = {"exact",
                              "minimum",
                              "maximum",
                              "text",
                              "blank",
                              "no",
                              "yes",
                              "real",
                              "plusZero",
                              "minusZero",
                              "letter",
                              "nothing",
                              "mutable"};
        const char *values[] = {
            "{\"mutable\":false,\"type\":\"integer\",\"value\":9007199254740993}",
            "{\"mutable\":false,\"type\":\"integer\",\"value\":-9223372036854775808}",
            "{\"mutable\":false,\"type\":\"integer\",\"value\":9223372036854775807}",
            "{\"mutable\":false,\"type\":\"string\",\"value\":\" a\\u0000b \"}",
            "{\"mutable\":false,\"type\":\"string\",\"value\":\"\"}",
            "{\"mutable\":false,\"type\":\"boolean\",\"value\":false}",
            "{\"mutable\":false,\"type\":\"boolean\",\"value\":true}",
            "{\"mutable\":false,\"type\":\"real\",\"value\":\"3ff8000000000000\"}",
            "{\"mutable\":false,\"type\":\"real\",\"value\":\"0000000000000000\"}",
            "{\"mutable\":false,\"type\":\"real\",\"value\":\"8000000000000000\"}",
            "{\"mutable\":false,\"type\":\"character\",\"value\":\"Я\"}",
            "{\"mutable\":false,\"type\":\"null\"}",
            "{\"mutable\":true}"};
        for (size_t i = 0; i < N(keys); i++)
            equal(r, yyjson_obj_get(fields, keys[i]), values[i]);
        REQUIRE(!yyjson_obj_get(fields, "hidden") && !yyjson_obj_get(fields, "secret"));
        REQUIRE(yyjson_obj_get(fields, "dynamic")
                && !yyjson_obj_get(yyjson_obj_get(fields, "dynamic"), "value"));
    } else if (!strcmp(name, "function-identities")) {
        yyjson_val *functions = yyjson_obj_get(doc, "functions");
        REQUIRE(yyjson_arr_size(functions) == 6);
        unsigned files[] = {0, 0, 0, 1, 2, 2};
        const char *parameters[] = {"[{\"name\":\"x\"}]",
                                    "[]",
                                    "[{\"name\":\"value\"}]",
                                    "[]",
                                    "[{\"name\":\"x\"}]",
                                    "[]"};
        for (size_t i = 0; i < 6; i++) {
            yyjson_val *f = yyjson_arr_get(functions, i), *id = yyjson_obj_get(f, "functionId"),
                       *file = yyjson_obj_get(f, "file");
            REQUIRE(yyjson_is_uint(id) && yyjson_get_uint(id) == i + 1);
            REQUIRE(yyjson_is_uint(file) && yyjson_get_uint(file) == files[i]);
            equal(r, yyjson_obj_get(f, "parameters"), parameters[i]);
        }
        equal(r, yyjson_obj_get(yyjson_obj_get(fields, "factory"), "functionId"), "1");
        equal(r, yyjson_obj_get(yyjson_obj_get(fields, "again"), "functionId"), "1");
        equal(r, yyjson_obj_get(yyjson_obj_get(fields, "sibling"), "functionId"), "3");
        equal(r, yyjson_obj_get(fields, "replaceable"), "{\"mutable\":true}");
    } else if (!strcmp(name, "metadata-consumer")) {
        if (!app) {
            summaries(r, export_function(r, doc, "identity"), true);
        } else {
            equal(r,
                  yyjson_obj_get(fields, "exact"),
                  "{\"mutable\":false,\"type\":\"integer\",\"value\":9007199254740993}");
            equal(r, yyjson_obj_get(yyjson_obj_get(fields, "result"), "type"), "\"integer\"");
            equal(r, yyjson_obj_get(fields, "unknown"), "{\"mutable\":false}");
        }
    } else if (!strcmp(name, "reexports")) {
        yyjson_val *answer = yyjson_obj_get(fields, "answer");
        equal(r,
              yyjson_obj_get(answer, "functionRef"),
              "{\"module\":\"owner\",\"version\":\"1\",\"functionId\":1}");
        REQUIRE(!yyjson_obj_get(answer, "functionId"));
        if (!strcmp(filename, "modules/top/1.gbin")) {
            yyjson_val *deps = yyjson_obj_get(doc, "dependencies");
            REQUIRE(yyjson_arr_size(deps) == 1);
            equal(r, yyjson_obj_get(yyjson_arr_get(deps, 0), "name"), "\"middle\"");
            equal(r, yyjson_obj_get(yyjson_arr_get(deps, 0), "version"), "\"1\"");
        }
    } else if (!strcmp(name, "proof-boundaries")) {
        equal(r, yyjson_obj_get(fields, "later"), "{\"mutable\":false}");
        REQUIRE(!yyjson_obj_get(fields, "nested") && !yyjson_obj_get(fields, "notMain"));
        yyjson_val *a = summaries(r, function(r, doc, 2), false), *v;
        size_t i, n;
        yyjson_arr_foreach(a, i, n, v) {
            REQUIRE(!yyjson_obj_get(v, "result"));
        }
        REQUIRE(!yyjson_obj_get(export_function(r, doc, "callback"), "analysis"));
        REQUIRE(!yyjson_obj_get(export_function(r, doc, "uncertain"), "analysis"));
    } else if (!strcmp(name, "mutable-function-contract")) {
        if (!app)
            equal(r, yyjson_obj_get(fields, "calculate"), "{\"mutable\":true}");
        else {
            equal(r, yyjson_obj_get(fields, "first"), "{\"mutable\":false}");
            equal(r, yyjson_obj_get(fields, "second"), "{\"mutable\":false}");
        }
    } else if (!strcmp(name, "constant-function-effects")) {
        if (app) {
            REQUIRE(yyjson_obj_get(fields, "first")
                    && !yyjson_obj_get(yyjson_obj_get(fields, "first"), "value"));
            REQUIRE(yyjson_obj_get(fields, "second")
                    && !yyjson_obj_get(yyjson_obj_get(fields, "second"), "value"));
        } else {
            yyjson_val *a = summaries(r, export_function(r, doc, "next"), true), *v;
            size_t i, n;
            bool found = false;
            yyjson_arr_foreach(a, i, n, v) {
                REQUIRE(!yyjson_obj_get(yyjson_obj_get(v, "result"), "value"));
                yyjson_val *may = yyjson_obj_get(yyjson_obj_get(v, "effects"), "may"), *effect;
                bool read = false, write = false, other = false;
                size_t j, m;
                REQUIRE(!may || yyjson_is_arr(may));
                yyjson_arr_foreach(may, j, m, effect) {
                    const char *s = yyjson_get_str(effect);
                    REQUIRE(s != NULL);
                    if (!strcmp(s, "externalRead"))
                        read = true;
                    else if (!strcmp(s, "externalWrite"))
                        write = true;
                    else
                        other = true;
                }
                if (read && write && !other)
                    found = true;
            }
            REQUIRE(found);
        }
    } else if (!strcmp(name, "callback-contract")) {
        if (!app)
            REQUIRE(!yyjson_obj_get(export_function(r, doc, "apply"), "analysis"));
        else
            REQUIRE(yyjson_obj_get(fields, "observed")
                    && !yyjson_obj_get(yyjson_obj_get(fields, "observed"), "value"));
    } else if (!strcmp(name, "result-alias-contract") || !strcmp(name, "nested-sharing-contract")) {
        yyjson_val *observed = yyjson_obj_get(fields, "observed");
        REQUIRE(observed != NULL);
        const char *type = yyjson_get_str(yyjson_obj_get(observed, "type"));
        REQUIRE(!type || strcmp(type, "integer"));
    } else if (!strcmp(name, "instrumented-metadata-contract")) {
        summaries(r, export_function(r, doc, "twice"), true);
    } else
        REQUIRE(false);
}

static bool contains(const void *bytes, size_t size, const char *needle) {
    size_t n = strlen(needle);
    if (n > size)
        return false;
    for (size_t i = 0; i <= size - n; i++)
        if (!memcmp((const char *)bytes + i, needle, n))
            return true;
    return false;
}

static void body(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *name = field(r, c->fixture, "name");
    module_execution_suite_t *suite = module_execution_suite(r, c->host);
    module_execution_project_t *p = module_execution_project(suite, name);
    yyjson_val *operations = yyjson_obj_get(c->fixture, "operations"), *op;
    size_t i, n;
    REQUIRE(yyjson_is_arr(operations) && yyjson_arr_size(operations));
    yyjson_arr_foreach(operations, i, n, op) {
        const char *mode = field(r, op, "mode");
        if (!strcmp(mode, "compile")) {
            yyjson_val *values = yyjson_obj_get(op, "files"), *key, *value;
            REQUIRE(yyjson_is_obj(values));
            size_t j, count = yyjson_obj_size(values), total;
            module_execution_file_t *files =
                language_owned(r, CALLOC((count + 1) * sizeof(*files)));
            yyjson_obj_foreach(values, j, total, key, value) {
                const char *s = yyjson_get_str(value), *filename = yyjson_get_str(key);
                REQUIRE(s && filename && strlen(s) == yyjson_get_len(value)
                        && strlen(filename) == yyjson_get_len(key));
                files[j] = (module_execution_file_t){filename, s};
            }
            REQUIRE(yyjson_is_null(yyjson_obj_get(op, "dependencies")));
            yyjson_val *fail = yyjson_obj_get(op, "fail");
            REQUIRE(yyjson_is_bool(fail));
            module_execution_compile(p,
                                     field(r, op, "name"),
                                     field(r, op, "code"),
                                     field(r, op, "version"),
                                     files,
                                     count,
                                     NULL,
                                     yyjson_get_bool(fail));
        } else {
            const char *relative_path = field(r, op, "path"), *file = path(r, p, relative_path);
            if (!strcmp(mode, "remove")) {
                fs_error_t error = {0};
                bool removed = remove_fs_tree(file, &error);
                clear_fs_error(&error);
                REQUIRE(removed);
                fs_info_t info;
                bool exists = inspect_fs_path(file, false, &info, &error);
                REQUIRE(!exists && error.status == FS_NOT_FOUND);
                clear_fs_error(&error);
            } else if (!strcmp(mode, "metadata"))
                check_metadata(r, name, relative_path, metadata(r, file));
            else {
                REQUIRE(!strcmp(mode, "invoke"));
                const char *command = field(r, op, "command"), *expected = field(r, op, "expected"),
                           *output = NULL;
                REQUIRE(!strcmp(command, "compile") || !strcmp(command, "binary")
                        || !strcmp(command, "compile-contract")
                        || !strcmp(command, "metadata-contract")
                        || !strcmp(command, "binary-contract"));
                yyjson_val *v = yyjson_obj_get(op, "output"), *fail = yyjson_obj_get(op, "fail");
                REQUIRE(yyjson_is_bool(fail));
                if (!yyjson_is_null(v))
                    output = path(r, p, field(r, op, "output"));
                scenario_command_result_t result = module_execution_invoke(p,
                                                                           command,
                                                                           file,
                                                                           output,
                                                                           expected,
                                                                           strlen(expected),
                                                                           yyjson_get_bool(fail));
                if (!strcmp(name, "static-call-diagnostics") && !strcmp(command, "compile")) {
                    if (!strcmp(relative_path, "sources/dynamic-1/module.gmod"))
                        REQUIRE(!contains(result.err, result.err_size, "not callable"));
                    else
                        REQUIRE(contains(result.err,
                                         result.err_size,
                                         "statically known value is not callable"));
                }
            }
        }
    }
    printf("%s: %zu compile/VM checks passed\n", name, module_execution_count(suite));
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
    if (argc == 6 || argc == 7) {
        fs_error_t e = {0};
        char *host = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        char *filename = join_fs_path(argv[5], "module-interface/scenarios.json", &e);
        clear_fs_error(&e);
        void *data = NULL;
        size_t size = 0;
        bool read = filename && read_fs_regular_file(filename, 1024 * 1024, &data, &size, &e);
        clear_fs_error(&e);
        yyjson_doc *doc = read ? yyjson_read(data, size, 0) : NULL;
        yyjson_val *array = yyjson_doc_get_root(doc);
        size_t count = yyjson_arr_size(array);
        if (host && yyjson_is_arr(array) && count) {
            configuration_t *c = CALLOC(count * sizeof(*c));
            scenario_definition_t *defs = CALLOC(count * sizeof(*defs));
            for (size_t i = 0; i < count; i++) {
                yyjson_val *v = yyjson_arr_get(array, i);
                c[i] = (configuration_t){host, v};
                defs[i] = (scenario_definition_t){yyjson_get_str(yyjson_obj_get(v, "id")),
                                                  120000,
                                                  body,
                                                  &c[i],
                                                  NULL};
            }
            scenario_options_t o = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(defs, count, argc == 7 ? 2 : 1, selection, &o);
            FREE(defs);
            FREE(c);
        } else
            fputs("Cannot load interface fixtures\n", stderr);
        yyjson_doc_free(doc);
        FREE(data);
        FREE(filename);
        FREE(host);
    } else
        fputs("usage: module_interface_scenarios ID HOST BUILD DATA FIXTURES [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
