/** @file module_build_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real native producer, immutable archive and compiler-failure contracts (#368).
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
#ifndef _WIN32
#    include <sys/utsname.h>
#endif
#define REQUIRE(x) scenario_require(r, (x), #x)

typedef struct {
    const void *data;
    size_t size;
} bytes_t;

typedef struct {
    const char *host, *compiler, *kind;
    yyjson_val *projects;
} configuration_t;

typedef struct {
    module_execution_project_t *project;
    const char *descriptor, *output;
} project_t;

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool free_malloc(void *p) {
    free(p);
    return true;
}

static const char *field(scenario_run_t *r, yyjson_val *v, const char *key) {
    yyjson_val *x = yyjson_obj_get(v, key);
    REQUIRE(yyjson_is_str(x) && yyjson_get_len(x) == strlen(yyjson_get_str(x)));
    return yyjson_get_str(x);
}

static yyjson_val *parse(scenario_run_t *r, bytes_t bytes) {
    yyjson_doc *d = yyjson_read(bytes.data, bytes.size, 0);
    REQUIRE(d);
    scenario_defer(r, free_doc, d);
    return yyjson_doc_get_root(d);
}

static bytes_t read_bytes(scenario_run_t *r, const char *path) {
    size_t size;
    const char *data = language_read(r, path, &size);
    return (bytes_t){data, size};
}

static void equal(scenario_run_t *r, const char *label, bytes_t a, bytes_t b) {
    scenario_require_bytes(r, label, a.data, a.size, b.data, b.size, false);
}

static bool exists(scenario_run_t *r, const char *path) {
    fs_info_t info;
    fs_error_t error = {0};
    bool found = inspect_fs_path(path, true, &info, &error), missing = error.status == FS_NOT_FOUND;
    clear_fs_error(&error);
    REQUIRE(found || missing);
    if (found)
        REQUIRE(info.kind == FS_FILE);
    return found;
}

static bytes_t part(scenario_run_t *r, bytes_t archive, const char *name) {
    format_zip_info_t info;
    char error[256];
    REQUIRE(format_zip_inspect(archive.data, archive.size, SIZE_MAX, &info, error, sizeof(error)));
    size_t count = info.count;
    for (size_t i = 0; i < count; i++) {
        REQUIRE(format_zip_inspect(archive.data, archive.size, i, &info, error, sizeof(error)));
        if (info.name_size == strlen(name) && !memcmp(info.name, name, info.name_size)) {
            size_t size;
            void *data = format_zip_extract(archive.data,
                                            archive.size,
                                            i,
                                            32 * 1024 * 1024,
                                            &size,
                                            error,
                                            sizeof(error));
            REQUIRE(data);
            scenario_defer(r, free_malloc, data);
            return (bytes_t){data, size};
        }
    }
    REQUIRE(false);
    return (bytes_t){0};
}

static yyjson_val *metadata(scenario_run_t *r, bytes_t archive) {
    return parse(r, part(r, archive, "module.json"));
}

static project_t
project(scenario_run_t *r, configuration_t *c, module_execution_suite_t *suite, const char *name) {
    yyjson_val *v = NULL;
    for (size_t i = 0; i < yyjson_arr_size(c->projects); i++) {
        yyjson_val *x = yyjson_arr_get(c->projects, i);
        if (!strcmp(field(r, x, "name"), name)) {
            REQUIRE(!v);
            v = x;
        }
    }
    REQUIRE(v);
    module_execution_project_t *p = module_execution_project(suite, name);
    module_execution_put(p, "main.goat", field(r, v, "source"));
    yyjson_val *files = yyjson_obj_get(v, "files"), *key, *value;
    size_t i, n;
    REQUIRE(yyjson_is_obj(files));
    yyjson_obj_foreach(files, i, n, key, value) {
        REQUIRE(yyjson_is_str(value) && strlen(yyjson_get_str(value)) == yyjson_get_len(value));
        module_execution_put(p, yyjson_get_str(key), yyjson_get_str(value));
    }
    const char *descriptor = module_execution_put(p, "module.gmod", field(r, v, "descriptor"));
    return (project_t){p, descriptor, language_join(r, module_execution_root(p), "module.gbin")};
}

static scenario_command_result_t build(scenario_run_t *r,
                                       configuration_t *c,
                                       project_t p,
                                       const char *mode,
                                       const char *target,
                                       const char *compiler,
                                       bool success) {
    if (success && exists(r, p.output))
        language_remove(r, p.output);
    const char *args[] =
        {c->host, "native-build", p.descriptor, p.output, mode, target, compiler, NULL};
    scenario_command_result_t result = language_command(r, c->host, args, NULL);
    REQUIRE((result.exit_code == 0) == success);
    return result;
}

static bool contains(bytes_t bytes, const char *text) {
    size_t n = strlen(text);
    for (size_t i = 0; i + n <= bytes.size; i++)
        if (!memcmp((const char *)bytes.data + i, text, n))
            return true;
    return false;
}

static void diagnostic(scenario_run_t *r, scenario_command_result_t result, const char *message) {
    REQUIRE(contains((bytes_t){result.err, result.err_size}, message));
}

static void binary(scenario_run_t *r, configuration_t *c, const char *path, const char *expected) {
    const char *args[] = {c->host, "binary", path, NULL};
    scenario_command_result_t result = language_command(r, c->host, args, NULL);
    REQUIRE(result.exit_code == 0);
    scenario_require_bytes(r,
                           "binary initializer stdout",
                           expected,
                           strlen(expected),
                           result.out,
                           result.out_size,
                           true);
}

static void no_native(scenario_run_t *r, yyjson_val *doc) {
    yyjson_val *native = yyjson_obj_get(doc, "native");
    REQUIRE(!native || (yyjson_is_arr(native) && !yyjson_arr_size(native)));
}

static void run(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *which = c->kind;
    module_execution_suite_t *suite = module_execution_suite(r, c->host);
    const char *work = scenario_work_directory(scenario_run_context(r));
    const char *absent = language_join(r, work, "absent-compiler");
    REQUIRE(!exists(r, absent));
    if (!strcmp(which, "numeric")) {
        project_t p = project(r, c, suite, "numeric");
        scenario_command_result_t result =
            build(r, c, p, "descriptor", "descriptor", c->compiler, true);
        REQUIRE(!result.out_size);
        bytes_t archive = read_bytes(r, p.output);
        yyjson_val *doc = metadata(r, archive), *native = yyjson_obj_get(doc, "native");
        REQUIRE(yyjson_is_arr(native) && yyjson_arr_size(native) == 1);
        yyjson_val *v = yyjson_arr_get(native, 0), *compiler = yyjson_obj_get(v, "cCompiler");
        REQUIRE(*field(r, compiler, "name") && *field(r, compiler, "version"));
        bytes_t library = part(r, archive, field(r, v, "library"));
        char hash[65];
        format_binary_sha256(library.data, library.size, hash);
        REQUIRE(!strcmp(hash, field(r, v, "hash")));
        yyjson_val *target = yyjson_obj_get(v, "target"),
                   *features = yyjson_obj_get(target, "cpuFeatures");
        REQUIRE(yyjson_is_arr(features));
        bool cpu = false;
        if (yyjson_arr_size(features) == 1) {
            const char *s = yyjson_get_str(yyjson_arr_get(features, 0));
            cpu = s && (!strcmp(s, "sse2") || !strcmp(s, "i686"));
        } else if (yyjson_arr_size(features) == 2) {
            const char *a = yyjson_get_str(yyjson_arr_get(features, 0)),
                       *b = yyjson_get_str(yyjson_arr_get(features, 1));
            cpu = a && b && !strcmp(a, "fp") && !strcmp(b, "neon");
        }
        REQUIRE(cpu);
        yyjson_val *version = yyjson_obj_get(yyjson_obj_get(target, "runtime"), "minimumVersion");
        REQUIRE(yyjson_is_arr(version) && yyjson_arr_size(version));
        REQUIRE(yyjson_get_uint(yyjson_obj_get(v, "goatAbiVersion")) == 1);
        yyjson_val *fid = yyjson_obj_get(yyjson_obj_get(yyjson_obj_get(doc, "externals"), "square"),
                                         "functionId"),
                   *specializations = yyjson_obj_get(v, "specializations");
        REQUIRE(yyjson_is_uint(fid) && yyjson_is_arr(specializations));
        for (size_t i = 0; i < yyjson_arr_size(specializations); i++)
            REQUIRE(
                yyjson_equals(fid,
                              yyjson_obj_get(yyjson_arr_get(specializations, i), "functionId")));
        binary(r, c, p.output, "49\n");
#ifdef _WIN32
        const char *saved = language_join(r, work, "saved.dll");
#else
        const char *saved = language_join(r, work, "saved.so");
#endif
        language_write(r, saved, library.data, library.size);
        build(r, c, p, "off", "descriptor", absent, true);
        bytes_t plain = read_bytes(r, p.output);
        yyjson_val *plain_doc = metadata(r, plain);
        no_native(r, plain_doc);
        REQUIRE(!strcmp(field(r, doc, "mainHash"), field(r, plain_doc, "mainHash")));
        equal(r,
              "native/off bytecode identity",
              part(r, archive, "bytecode.bin"),
              part(r, plain, "bytecode.bin"));
    } else if (!strncmp(which, "empty-", 6)) {
        project_t p = project(r, c, suite, "empty");
        if (!strcmp(which, "empty-required")) {
            scenario_command_result_t result =
                build(r, c, p, "descriptor", "descriptor", c->compiler, false);
            REQUIRE(!result.out_size && !exists(r, p.output));
            diagnostic(r, result, "own specialization");
        } else {
            build(r, c, p, "off", "descriptor", absent, true);
            no_native(r, metadata(r, read_bytes(r, p.output)));
            if (!strcmp(which, "empty-auto-explicit"))
                build(r, c, p, "auto", "linux-x86_64-gnu", absent, false);
        }
    } else if (!strcmp(which, "included")) {
        project_t p = project(r, c, suite, "included");
        build(r, c, p, "descriptor", "descriptor", c->compiler, true);
        yyjson_val *doc = metadata(r, read_bytes(r, p.output)),
                   *v = yyjson_arr_get(yyjson_obj_get(doc, "native"), 0),
                   *specs = yyjson_obj_get(v, "specializations"),
                   *functions = yyjson_obj_get(doc, "functions");
        REQUIRE(yyjson_is_arr(specs) && yyjson_arr_size(specs) && yyjson_is_arr(functions));
        for (size_t i = 0; i < yyjson_arr_size(specs); i++) {
            yyjson_val *fid = yyjson_obj_get(yyjson_arr_get(specs, i), "functionId"), *found = NULL;
            REQUIRE(yyjson_is_uint(fid));
            for (size_t j = 0; j < yyjson_arr_size(functions); j++) {
                yyjson_val *f = yyjson_arr_get(functions, j);
                if (yyjson_equals(fid, yyjson_obj_get(f, "functionId"))) {
                    REQUIRE(!found);
                    found = f;
                }
            }
            REQUIRE(found && yyjson_get_uint(yyjson_obj_get(found, "file")) == 1);
        }
        binary(r, c, p.output, "42\n");
    } else if (!strcmp(which, "only-dependency")) {
        project_t dependency = project(r, c, suite, "dependency");
        build(r, c, dependency, "descriptor", "descriptor", c->compiler, true);
        bytes_t archive = read_bytes(r, dependency.output);
        language_write(r, language_join(r, work, "dependency@1.gbin"), archive.data, archive.size);
        project_t app = project(r, c, suite, "only-dependency");
        const char *directory =
            language_join(r, module_execution_modules(app.project), "dependency");
        language_directory(r, directory);
        language_write(r, language_join(r, directory, "1.gbin"), archive.data, archive.size);
        scenario_command_result_t result =
            build(r, c, app, "descriptor", "descriptor", c->compiler, false);
        REQUIRE(!result.out_size);
        diagnostic(r, result, "own specialization");
    } else {
        project_t p = project(r, c, suite, "missing");
        scenario_command_result_t result = build(r, c, p, "descriptor", "descriptor", absent, true);
        diagnostic(r, result, "warning:");
        no_native(r, metadata(r, read_bytes(r, p.output)));
        if (!strcmp(which, "missing-auto"))
            return;
        bytes_t previous = read_bytes(r, p.output);
        if (!strcmp(which, "missing-required"))
            build(r, c, p, "required", "descriptor", absent, false);
        else if (!strcmp(which, "unsupported-target"))
            build(r, c, p, "auto", "not-a-supported-target", c->compiler, false);
        else {
            const char *foreign = "linux-aarch64-gnu";
#ifndef _WIN32
            struct utsname platform;
            REQUIRE(uname(&platform) == 0);
            if (!strcmp(platform.machine, "aarch64"))
                foreign = "windows-x86_64-mingw64";
#endif
            REQUIRE(!strcmp(which, "foreign-target"));
            build(r, c, p, "auto", foreign, absent, false);
        }
        equal(r, "failure preserves previous archive", previous, read_bytes(r, p.output));
    }
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
        fs_error_t error = {0};
        char *host = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        char *path = join_fs_path(argv[6], "module-build/projects.json", &error);
        clear_fs_error(&error);
        void *raw = NULL;
        size_t size = 0;
        bool ok = host && path && read_fs_regular_file(path, 1024 * 1024, &raw, &size, &error);
        clear_fs_error(&error);
        yyjson_doc *doc = ok ? yyjson_read(raw, size, 0) : NULL;
        yyjson_val *projects = yyjson_doc_get_root(doc);
        if (yyjson_is_arr(projects) && yyjson_arr_size(projects)) {
            const char *names[] = {"numeric",
                                   "empty-required",
                                   "empty-off",
                                   "empty-auto-explicit",
                                   "included",
                                   "missing-auto",
                                   "missing-required",
                                   "unsupported-target",
                                   "foreign-target",
                                   "only-dependency"};

            enum { COUNT = sizeof(names) / sizeof(names[0]) };

            configuration_t configs[COUNT];
            scenario_definition_t definitions[COUNT];
            char ids[COUNT][96];
            for (size_t i = 0; i < COUNT; i++) {
                snprintf(ids[i], sizeof(ids[i]), "integration.module_build.%s", names[i]);
                configs[i] = (configuration_t){host, argv[3], names[i], projects};
                definitions[i] = (scenario_definition_t){ids[i], 360000, run, configs + i, NULL};
            }
            scenario_options_t options = {.build_directory = argv[4], .data_directory = argv[5]};
            const char *selection[] = {argv[1], argc == 8 ? argv[7] : NULL};
            result =
                scenario_registry_main(definitions, COUNT, argc == 8 ? 2 : 1, selection, &options);
        } else
            fputs("Cannot load native build projects\n", stderr);
        yyjson_doc_free(doc);
        FREE(raw);
        FREE(path);
        FREE(host);
    } else
        fputs("usage: module_build_scenarios ID BUILDER CC BUILD DATA FIXTURES [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
