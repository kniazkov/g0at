/** @file native_artifact_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Frozen export/descriptor corpus and real non-loading constructor proof (#380).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)

typedef struct {
    const char *host, *compiler, *fixtures;
    yyjson_val *fixture;
} config_t;

static const char *field(scenario_run_t *r, yyjson_val *object, const char *name) {
    yyjson_val *value = yyjson_obj_get(object, name);
    const char *text = yyjson_get_str(value);
    REQUIRE(text && strlen(text) == yyjson_get_len(value));
    return text;
}

static bool utf8(const unsigned char *p, size_t n) {
    for (size_t i = 0; i < n;) {
        unsigned c = p[i++], value, remaining, minimum;
        if (c < 128)
            continue;
        if (c >= 0xc2 && c <= 0xdf) {
            value = c & 31;
            remaining = 1;
            minimum = 128;
        } else if (c >= 0xe0 && c <= 0xef) {
            value = c & 15;
            remaining = 2;
            minimum = 2048;
        } else if (c >= 0xf0 && c <= 0xf4) {
            value = c & 7;
            remaining = 3;
            minimum = 65536;
        } else
            return false;
        if (remaining > n - i)
            return false;
        while (remaining--) {
            c = p[i++];
            if ((c & 0xc0) != 0x80)
                return false;
            value = (value << 6) | (c & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return false;
    }
    return true;
}

typedef struct {
    child_options_t options;
    scenario_command_result_t result;
} command_t;

static void command(scenario_run_t *r, void *user) {
    command_t *call = user;
    call->result = scenario_command_capture(r, &call->options);
}

static const char *copy_fixture(scenario_run_t *r, config_t *c, const char *key, const char *name) {
    const char *relative = field(r, c->fixture, key);
    REQUIRE(relative[0] != '/' && !strstr(relative, "..") && !strchr(relative, '\\')
            && !strchr(relative, ':'));
    size_t size;
    const char *bytes = language_read(r, language_join(r, c->fixtures, relative), &size);
    const char *path = language_join(r, scenario_work_directory(scenario_run_context(r)), name);
    language_write(r, path, bytes, size);
    return path;
}

static scenario_command_result_t invoke(scenario_run_t *r, const char *const *args, uint64_t ms) {
    command_t call = {
        .options = {.executable = args[0],
                    .arguments = args,
                    .working_directory = scenario_work_directory(scenario_run_context(r)),
                    .empty_stdin = true}};
    scenario_with_timeout(r, ms, command, &call);
    return call.result;
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
    const char *kind = field(r, c->fixture, "kind");
    const char *args[5] = {c->host, NULL, NULL, NULL, NULL};
    if (!strcmp(kind, "compile")) {
        const char *source = copy_fixture(r, c, "source", "fixture.c");
#ifdef _WIN32
        const char *name = "fixture.dll";
#else
        const char *name = "fixture.so";
#endif
        const char *library =
            language_join(r, scenario_work_directory(scenario_run_context(r)), name);
        const char *compile[] = {c->compiler, "-shared", "-fPIC", source, "-o", library, NULL};
        scenario_command_result_t result = invoke(r, compile, 30000);
        REQUIRE(result.exit_code == 0);
        args[1] = library;
        args[2] = "adapter";
    } else if (!strcmp(kind, "validate")) {
        args[1] = "--validate";
        args[2] = copy_fixture(r, c, "image", "checked.bin");
        args[3] = copy_fixture(r, c, "metadata", "metadata.json");
    } else {
        REQUIRE(!strcmp(kind, "inspect"));
        args[1] = copy_fixture(r, c, "image", "image.bin");
        args[2] = field(r, c->fixture, "symbol");
    }
    scenario_command_result_t result = invoke(r, args, 10000);
    REQUIRE(utf8(result.out, result.out_size));
    REQUIRE(utf8(result.err, result.err_size));
    yyjson_val *expected = yyjson_obj_get(c->fixture, "succeeds");
    REQUIRE(yyjson_is_bool(expected));
    REQUIRE((result.exit_code == 0) == yyjson_get_bool(expected));
    /* Original output content is unspecified: only strict text decoding and status. */
}

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
    int result = 1;
    if (argc == 7 || (argc == 8 && !strcmp(argv[7], "--keep-work"))) {
        fs_error_t error = {0};
        char *host = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        char *compiler_path = NULL;
        bool explicit_compiler = strchr(argv[3], '/') || strchr(argv[3], '\\');
        if (explicit_compiler)
            compiler_path = absolute_fs_path(argv[3], &error);
        clear_fs_error(&error);
        char *path = join_fs_path(argv[6], "scenarios.json", &error);
        clear_fs_error(&error);
        void *data = NULL;
        size_t size = 0;
        bool read = path && read_fs_regular_file(path, 1024 * 1024, &data, &size, &error);
        clear_fs_error(&error);
        yyjson_doc *doc = read ? yyjson_read(data, size, 0) : NULL;
        yyjson_val *fixtures = yyjson_doc_get_root(doc);
        size_t count = yyjson_arr_size(fixtures);
        if (host && (!explicit_compiler || compiler_path) && yyjson_is_arr(fixtures) && count) {
            config_t *configs = CALLOC(count * sizeof(*configs));
            scenario_definition_t *defs = CALLOC(count * sizeof(*defs));
            for (size_t i = 0; i < count; i++) {
                yyjson_val *fixture = yyjson_arr_get(fixtures, i);
                configs[i] =
                    (config_t){host, compiler_path ? compiler_path : argv[3], argv[6], fixture};
                defs[i] = (scenario_definition_t){yyjson_get_str(yyjson_obj_get(fixture, "id")),
                                                  60000,
                                                  body,
                                                  configs + i,
                                                  NULL};
            }
            scenario_options_t options = {.build_directory = argv[4], .data_directory = argv[5]};
            const char *selection[] = {argv[1], argc == 8 ? argv[7] : NULL};
            result = scenario_registry_main(defs, count, argc == 8 ? 2 : 1, selection, &options);
            FREE(defs);
            FREE(configs);
        } else
            fputs("Cannot load native-artifact fixtures\n", stderr);
        yyjson_doc_free(doc);
        FREE(data);
        FREE(path);
        FREE(host);
        FREE(compiler_path);
    } else
        fputs("usage: native_artifact_scenarios ID HOST CC BUILD DATA FIXTURES [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
