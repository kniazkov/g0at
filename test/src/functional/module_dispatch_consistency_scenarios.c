/** @file module_dispatch_consistency_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real per-call dispatch and cross-part identity contracts (#365).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "test/support/format_binary.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/module_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)

typedef struct {
    const char *dispatch, *consistency, *goat, *fixtures;
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

static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static unsigned digit(scenario_run_t *r, char c) {
    if (c >= '0' && c <= '9')
        return (unsigned)(c - '0');
    if (c >= 'a' && c <= 'f')
        return (unsigned)(c - 'a' + 10);
    REQUIRE(false);
    return 0;
}

static const char *text(scenario_run_t *r, const void *raw, size_t n) {
    string_value_t decoded = decode_utf8_n(raw, n);
    REQUIRE(decoded.data || !n);
    if (decoded.should_free)
        language_owned(r, (gchar_t *)decoded.data);
    size_t encoded_size;
    char *encoded = encode_utf8_view((string_view_t){decoded.data, decoded.length}, &encoded_size);
    REQUIRE(encoded);
    language_owned(r, encoded);
    REQUIRE(n == encoded_size && !memcmp(raw, encoded, n));
    return encoded;
}

static scenario_command_result_t
command(scenario_run_t *r, const char *exe, const char *const *args) {
    child_options_t options = {.executable = exe, .arguments = args};
    return scenario_command_capture(r, &options);
}

static void dispatch(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    yyjson_val *v = c->fixture;
    module_execution_project_t *project =
        module_execution_project(module_execution_suite(r, c->dispatch), "dispatch");
    const char *source = module_execution_put(project, "main.goat", field(r, v, "source"));
    yyjson_val *files = yyjson_obj_get(v, "files"), *key, *value;
    REQUIRE(yyjson_is_obj(files));
    size_t i, n;
    yyjson_obj_foreach(files, i, n, key, value) {
        REQUIRE(yyjson_is_str(value) && strlen(yyjson_get_str(value)) == yyjson_get_len(value));
        module_execution_put(project, yyjson_get_str(key), yyjson_get_str(value));
    }
    size_t size;
    char *json = yyjson_val_write(yyjson_obj_get(v, "configuration"), 0, &size);
    REQUIRE(json);
    scenario_defer(r, free_malloc, json);
    const char *config = module_execution_put(project, "dispatch.json", json),
               *mode = field(r, v, "mode");
    REQUIRE(!strcmp(mode, "auto") || !strcmp(mode, "required") || !strcmp(mode, "off"));
    const char *args[] = {c->dispatch, source, config, mode, NULL};
    scenario_command_result_t result = command(r, c->dispatch, args);
    yyjson_val *fail = yyjson_obj_get(v, "fail"), *expected = yyjson_obj_get(v, "expected");
    REQUIRE(yyjson_is_bool(fail) && yyjson_is_str(expected));
    REQUIRE((result.exit_code != 0) == yyjson_get_bool(fail));
    text(r, result.out, result.out_size);
    scenario_require_bytes(r,
                           "dispatch stdout",
                           yyjson_get_str(expected),
                           yyjson_get_len(expected),
                           result.out,
                           result.out_size,
                           true);
    const char *error = text(r, result.err, result.err_size);
    /* Match the first METRICS line, not a later line that might conceal a retry. */
    const char *marker = NULL;
    size_t marker_size = 0;
    for (size_t offset = 0; offset < result.err_size;) {
        size_t end = offset;
        while (end < result.err_size && error[end] != '\n' && error[end] != '\r')
            end++;
        if (end - offset >= 8 && !memcmp(error + offset, "METRICS ", 8)) {
            marker = error + offset + 8;
            marker_size = end - offset - 8;
            break;
        }
        offset = end;
        while (offset < result.err_size && (error[offset] == '\r' || error[offset] == '\n'))
            offset++;
    }
    REQUIRE(marker);
    yyjson_doc *metrics = yyjson_read(marker, marker_size, 0);
    REQUIRE(metrics);
    scenario_defer(r, free_doc, metrics);
    yyjson_val *root = yyjson_doc_get_root(metrics), *calls = yyjson_obj_get(root, "calls"),
               *retries = yyjson_obj_get(root, "retries");
    REQUIRE(yyjson_is_uint(calls) && yyjson_equals(calls, yyjson_obj_get(v, "calls")));
    REQUIRE(yyjson_is_uint(retries) && yyjson_get_uint(retries) == 0);
    yyjson_val *resolutions = yyjson_obj_get(v, "resolutions");
    if (!yyjson_is_null(resolutions))
        REQUIRE(yyjson_equals(yyjson_obj_get(root, "resolutions"), resolutions));
    yyjson_val *diagnostic = yyjson_obj_get(v, "diagnostic");
    if (!yyjson_is_null(diagnostic)) {
        const char *part = field(r, v, "diagnostic");
        REQUIRE(strstr(error, part));
    }
}

static void consistency(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *source = field(r, c->fixture, "inputHex");
    size_t size = strlen(source) / 2;
    REQUIRE(!(strlen(source) % 2));
    unsigned char *bytes = language_owned(r, ALLOC(size + 1));
    for (size_t i = 0; i < size; i++)
        bytes[i] = (unsigned char)(digit(r, source[2 * i]) * 16 + digit(r, source[2 * i + 1]));
    REQUIRE(size >= 4);
    uint32_t count = u32(bytes);
    REQUIRE(count && count < 1024);
    size_t offset = 4;
    /* Framing/ZIP inspection is independent of the production consistency host.
     * Semantic corruptions are retained verbatim, never repaired/rehashed here. */
    for (uint32_t i = 0; i < count; i++) {
        REQUIRE(offset <= size && size - offset >= 4);
        size_t length = u32(bytes + offset);
        offset += 4;
        REQUIRE(length <= size - offset);
        format_zip_info_t info;
        char error[256];
        REQUIRE(format_zip_inspect(bytes + offset, length, SIZE_MAX, &info, error, sizeof(error)));
        offset += length;
    }
    const char *mode = field(r, c->fixture, "mode");
    bool indexed = !strcmp(mode, "descriptor") || !strcmp(mode, "graph");
    REQUIRE(indexed || !strcmp(mode, "off") || !strcmp(mode, "auto") || !strcmp(mode, "required"));
    yyjson_val *index = yyjson_obj_get(c->fixture, "index"),
               *expected = yyjson_obj_get(c->fixture, "expected");
    REQUIRE(yyjson_is_uint(index) && yyjson_is_uint(expected));
    char number[32];
    snprintf(number, sizeof(number), "%llu", (unsigned long long)yyjson_get_uint(index));
    const char *args[] = {c->consistency, mode, indexed ? number : NULL, NULL};
    child_options_t options = {.executable = c->consistency,
                               .arguments = args,
                               .stdin_data = bytes,
                               .stdin_size = size};
    scenario_command_result_t result = scenario_command_capture(r, &options);
    REQUIRE(result.exit_code == 0);
    snprintf(number, sizeof(number), "%llu\n", (unsigned long long)yyjson_get_uint(expected));
    scenario_require_bytes(r,
                           "consistency status",
                           number,
                           strlen(number),
                           result.out,
                           result.out_size,
                           true);
}

static void identity(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *args[] = {c->dispatch, "--identity", NULL};
    scenario_command_result_t result = command(r, c->dispatch, args);
    REQUIRE(result.exit_code == 0);
    const unsigned char *s = result.out;
    size_t begin = 0, end = result.out_size;
    while (begin < end && isspace(s[begin]))
        begin++;
    while (end > begin && isspace(s[end - 1]))
        end--;
    scenario_require_bytes(r, "module instance identity", "OK", 2, s + begin, end - begin, false);
}

static void api(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *args[] = {c->consistency, NULL};
    REQUIRE(command(r, c->consistency, args).exit_code == 0);
}

static void producer(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    size_t source_size;
    const char *source =
        language_read(r,
                      language_join(r, c->fixtures, "module-dispatch-consistency/identities.goat"),
                      &source_size);
    const char *optimizations[] = {"none", "all", "all", "all"},
               *modes[] = {"off", "off", "auto", "required"};
    const char *root = scenario_work_directory(scenario_run_context(r)), *last = NULL;
    uint32_t baseline[6][6];
    for (size_t i = 0; i < 4; i++) {
        char name[64];
        snprintf(name, sizeof(name), "%s-%s.goat", optimizations[i], modes[i]);
        const char *path = language_join(r, root, name);
        last = path;
        language_write(r, path, source, source_size);
        const char *args[] = {c->goat,
                              "--compile",
                              "--optimize",
                              optimizations[i],
                              "--native",
                              modes[i],
                              path,
                              NULL};
        REQUIRE(command(r, c->goat, args).exit_code == 0);
        snprintf(name, sizeof(name), "%s-%s.gbin", optimizations[i], modes[i]);
        size_t size;
        const unsigned char *bytes =
            (const unsigned char *)language_read(r, language_join(r, root, name), &size);
        if (size >= 8 && !memcmp(bytes, "GOATPG01", 8)) {
            REQUIRE(size >= 96);
            bytes += 96;
            size -= 96;
        }
        REQUIRE(size >= 64 && !memcmp(bytes, "GOATBC01", 8));
        size_t image_size = u32(bytes + 16);
        REQUIRE(image_size <= size);
        char error[256];
        REQUIRE(format_binary_check(bytes, image_size, error, sizeof(error)));
        uint32_t count = u32(bytes + 32), offset = u32(bytes + 52);
        REQUIRE(count == 6 && offset <= image_size && image_size - offset >= 6 * 32);
        for (size_t j = 0; j < 6; j++)
            for (size_t k = 0; k < 6; k++) {
                uint32_t value = u32(bytes + offset + j * 32 + k * 4);
                if (!i)
                    baseline[j][k] = value;
                else
                    scenario_require(
                        r,
                        value == baseline[j][k],
                        "producer function table changed across optimization/native mode");
                if (!k)
                    scenario_require(r,
                                     value == j + 1,
                                     "producer function identity is not stable 1..6");
            }
    }
    const char *args[] = {c->goat, "--print-c", last, NULL};
    scenario_command_result_t generated = command(r, c->goat, args);
    REQUIRE(generated.exit_code == 0);
    const char *code = text(r, generated.out, generated.out_size);
    REQUIRE(strlen(code) == generated.out_size);
    REQUIRE(strstr(code, "g_f2_i_") && strstr(code, "g_f3_i_"));
    REQUIRE(!strstr(code, "g_f4_") && !strstr(code, "g_f5_") && !strstr(code, "g_f6_"));
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
    if (argc == 8 || argc == 9) {
        fs_error_t e = {0};
        char *hosts[3] = {0};
        bool ok = true;
        for (size_t j = 0; j < 3; j++) {
            hosts[j] = absolute_fs_path(argv[j + 2], &e);
            clear_fs_error(&e);
            ok = ok && hosts[j];
        }
        yyjson_doc *docs[2] = {0};
        void *data[2] = {0};
        const char *names[] = {"dispatch.json", "consistency.json"};
        size_t count = 3;
        for (size_t j = 0; j < 2; j++) {
            char relative[80];
            snprintf(relative, sizeof(relative), "module-dispatch-consistency/%s", names[j]);
            char *path = join_fs_path(argv[7], relative, &e);
            clear_fs_error(&e);
            size_t size = 0;
            bool read = path && read_fs_regular_file(path, 2 * 1024 * 1024, &data[j], &size, &e);
            clear_fs_error(&e);
            FREE(path);
            docs[j] = read ? yyjson_read(data[j], size, 0) : NULL;
            yyjson_val *root = yyjson_doc_get_root(docs[j]);
            ok = ok && yyjson_is_arr(root) && yyjson_arr_size(root);
            count += yyjson_arr_size(root);
        }
        if (ok) {
            configuration_t *c = CALLOC(count * sizeof(*c));
            scenario_definition_t *defs = CALLOC(count * sizeof(*defs));
            const char *fixed[] = {"integration.module_dispatch.identity",
                                   "integration.module_consistency.api",
                                   "integration.module_consistency.producer-identities"};
            scenario_body_t bodies[] = {identity, api, producer};
            for (size_t j = 0; j < 3; j++) {
                c[j] = (configuration_t){hosts[0], hosts[1], hosts[2], argv[7], NULL};
                defs[j] = (scenario_definition_t){fixed[j],
                                                  j == 2 ? 300000 : 120000,
                                                  bodies[j],
                                                  &c[j],
                                                  NULL};
            }
            size_t index = 3;
            for (size_t j = 0; j < 2; j++) {
                yyjson_val *root = yyjson_doc_get_root(docs[j]);
                for (size_t i = 0; i < yyjson_arr_size(root); i++) {
                    yyjson_val *v = yyjson_arr_get(root, i);
                    c[index] = (configuration_t){hosts[0], hosts[1], hosts[2], argv[7], v};
                    defs[index] = (scenario_definition_t){yyjson_get_str(yyjson_obj_get(v, "id")),
                                                          120000,
                                                          j ? consistency : dispatch,
                                                          &c[index],
                                                          NULL};
                    index++;
                }
            }
            scenario_options_t options = {.build_directory = argv[5], .data_directory = argv[6]};
            const char *selection[] = {argv[1], argc == 9 ? argv[8] : NULL};
            result = scenario_registry_main(defs, count, argc == 9 ? 2 : 1, selection, &options);
            FREE(defs);
            FREE(c);
        } else
            fputs("Cannot load dispatch/consistency fixtures\n", stderr);
        for (size_t j = 0; j < 2; j++) {
            yyjson_doc_free(docs[j]);
            FREE(data[j]);
        }
        for (size_t j = 0; j < 3; j++)
            FREE(hosts[j]);
    } else
        fputs("usage: module_dispatch_consistency_scenarios ID DISPATCH CONSISTENCY GOAT BUILD "
              "DATA FIXTURES [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
