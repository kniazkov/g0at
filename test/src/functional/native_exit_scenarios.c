/** @file native_exit_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent native exit service and worker interruption scenarios (#382).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#    define SEP ';'
#    define EXT ".dll"
#else
#    include <unistd.h>
#    define SEP ':'
#    define EXT ".so"
#endif
#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})

typedef struct {
    const char *host, *cc, *repository, *fixtures, *cwd;
    yyjson_val *record;
} config_t;

static const char *string(scenario_run_t *r, const char *fmt, ...) {
    va_list a, b;
    va_start(a, fmt);
    va_copy(b, a);
    int n = vsnprintf(NULL, 0, fmt, b);
    va_end(b);
    REQUIRE(n >= 0);
    char *p = language_owned(r, ALLOC((size_t)n + 1));
    vsnprintf(p, (size_t)n + 1, fmt, a);
    va_end(a);
    return p;
}

static const char *absolute_compiler(scenario_run_t *r, const char *p) {
    fs_error_t e = {0};
    char *q = absolute_fs_path(p, &e);
    clear_fs_error(&e);
    REQUIRE(q);
    return language_owned(r, q);
}

static bool executable(scenario_run_t *r, const char *path) {
    if (!language_exists(r, path))
        return false;
#ifndef _WIN32
    return access(path, X_OK) == 0;
#else
    return true;
#endif
}

static const char *candidate(scenario_run_t *r, const char *path) {
    if (executable(r, path))
        return absolute_compiler(r, path);
#ifdef _WIN32
    char *raw = fs_environment("PATHEXT", NULL);
    const char *extensions = raw ? language_owned(r, raw) : ".COM;.EXE;.BAT;.CMD";
    const char *start = extensions;
    for (const char *p = start;; p++) {
        if (*p && *p != ';')
            continue;
        if (p > start) {
            const char *extended = string(r, "%s%.*s", path, (int)(p - start), start);
            if (executable(r, extended))
                return absolute_compiler(r, extended);
        }
        if (!*p)
            break;
        start = p + 1;
    }
#endif
    return NULL;
}

static const char *find_tool(scenario_run_t *r, const char *name) {
    if (strchr(name, '/') || strchr(name, '\\'))
        return candidate(r, name);
#ifdef _WIN32
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, NULL, 0);
    REQUIRE(count > 0);
    wchar_t *wide = language_owned(r, ALLOC((size_t)count * sizeof(*wide)));
    REQUIRE(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide, count) == count);
    if (NeedCurrentDirectoryForExePathW(wide)) {
        const char *found = candidate(r, language_join(r, ".", name));
        if (found)
            return found;
    }
#endif
    char *raw = fs_environment("PATH", NULL);
    if (!raw)
        return NULL;
    const char *start = language_owned(r, raw);
    for (const char *p = start;; p++) {
        if (*p && *p != SEP)
            continue;
        const char *dir = p == start ? "." : string(r, "%.*s", (int)(p - start), start);
        const char *found = candidate(r, language_join(r, dir, name));
        if (found)
            return found;
        if (!*p)
            break;
        start = p + 1;
    }
    return NULL;
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

static const char *field(scenario_run_t *r, yyjson_val *obj, const char *name) {
    yyjson_val *v = yyjson_obj_get(obj, name);
    const char *s = yyjson_get_str(v);
    REQUIRE(s && strlen(s) == yyjson_get_len(v));
    return s;
}

static bool boolean_field(scenario_run_t *r, yyjson_val *obj, const char *name) {
    yyjson_val *v = yyjson_obj_get(obj, name);
    REQUIRE(yyjson_is_bool(v));
    return yyjson_get_bool(v);
}

typedef struct {
    child_options_t options;
    scenario_command_result_t result;
} command_t;

static void capture(scenario_run_t *r, void *user) {
    command_t *c = user;
    c->result = scenario_command_capture(r, &c->options);
}

static scenario_command_result_t
command(scenario_run_t *r, config_t *c, const char *const *args, uint64_t timeout) {
    command_t call = {.options = {.executable = args[0],
                                  .arguments = args,
                                  .working_directory = c->cwd,
                                  .empty_stdin = true}};
    scenario_with_timeout(r, timeout, capture, &call);
    return call.result;
}

static bool has(const void *raw, size_t n, const char *text) {
    const char *p = raw;
    size_t m = strlen(text);
    for (size_t i = 0; m <= n && i <= n - m; i++)
        if (!memcmp(p + i, text, m))
            return true;
    return false;
}

static bool space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static void prefix(scenario_run_t *r, void *user) {
    config_t *c = user;
    scenario_command_result_t result = command(r, c, ARGS(c->host, "--exit-prefixes"), 10000);
    REQUIRE(result.exit_code == 0);
    const unsigned char *p = result.out;
    size_t start = 0, end = result.out_size;
    while (start < end && space(p[start]))
        start++;
    while (end > start && space(p[end - 1]))
        end--;
    scenario_require_bytes(r,
                           "exit service prefixes",
                           "OK",
                           2,
                           result.out_size ? p + start : (const unsigned char *)"",
                           end - start,
                           false);
}

static const char *legacy(scenario_run_t *r, config_t *c, const char *root) {
    const char *library = language_join(r, root, "legacy" EXT), *compiler = find_tool(r, c->cc);
    if (!compiler)
        compiler = c->cc;
    const char *args[] = {
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pedantic-errors",
        "-O2",
        "-DGOAT_NATIVE_BUILD",
        string(r, "-I%s", language_join(r, c->repository, "include")),
        "-shared",
#ifndef _WIN32
        "-fPIC",
#endif
        language_join(r, c->repository, "test/src/functional/native_exit/provider.c"),
        "-o",
        library,
        NULL};
    scenario_command_result_t result = command(r, c, args, 60000);
    REQUIRE(result.exit_code == 0);
    return library;
}

static void write_text(scenario_run_t *r, const char *path, const char *data, size_t size) {
    REQUIRE(utf8((const unsigned char *)data, size));
#ifdef _WIN32
    unsigned char *translated = language_owned(r, ALLOC(2 * size + 1));
    size_t n = 0;
    for (size_t i = 0; i < size; i++) {
        if (data[i] == '\n')
            translated[n++] = '\r';
        translated[n++] = (unsigned char)data[i];
    }
    language_write(r, path, translated, n);
#else
    language_write(r, path, data, size);
#endif
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
    yyjson_val *record = c->record;
    const char *name = field(r, record, "name");
    REQUIRE(!strchr(name, '/') && !strchr(name, '\\') && strcmp(name, ".") && strcmp(name, ".."));
    const char *data = language_join(r, c->fixtures, name),
               *root = scenario_work_directory(scenario_run_context(r));
    bool old = boolean_field(r, record, "legacy"), terminal = boolean_field(r, record, "terminal"),
         exited = boolean_field(r, record, "exited");
    const char *library = old ? legacy(r, c, root) : NULL;
    yyjson_val *files = yyjson_obj_get(record, "files");
    REQUIRE(yyjson_is_arr(files));
    for (size_t i = 0; i < yyjson_arr_size(files); i++) {
        yyjson_val *v = yyjson_arr_get(files, i);
        const char *file = yyjson_get_str(v);
        REQUIRE(file && strlen(file) == yyjson_get_len(v));
        REQUIRE(!strcmp(file, "main.goat") || !strcmp(file, "init.goat")
                || !strcmp(file, "config.json"));
        size_t n;
        const char *bytes = language_read(r, language_join(r, data, file), &n);
        if (old && !strcmp(file, "config.json")) {
            const char needle[] = "$LEGACY_LIBRARY";
            const char *at = NULL;
            for (size_t j = 0; j + sizeof(needle) - 1 <= n; j++)
                if (!memcmp(bytes + j, needle, sizeof(needle) - 1)) {
                    REQUIRE(!at);
                    at = bytes + j;
                }
            REQUIRE(at);
            /* Escape only the runtime path, preserving every frozen byte around it. */
            size_t len = strlen(library);
            char *escaped = language_owned(r, ALLOC(6 * len + 1));
            size_t used = 0;
            for (size_t j = 0; j < len; j++) {
                unsigned char ch = (unsigned char)library[j];
                if (ch == '"' || ch == '\\') {
                    escaped[used++] = '\\';
                    escaped[used++] = (char)ch;
                } else if (ch < 32) {
                    snprintf(escaped + used, 7, "\\u%04x", ch);
                    used += 6;
                } else
                    escaped[used++] = (char)ch;
            }
            size_t before = (size_t)(at - bytes), after = before + sizeof(needle) - 1,
                   total = n - (sizeof(needle) - 1) + used;
            char *patched = language_owned(r, ALLOC(total + 1));
            memcpy(patched, bytes, before);
            memcpy(patched + before, escaped, used);
            memcpy(patched + before + used, bytes + after, n - after);
            bytes = patched;
            n = total;
        }
        write_text(r, language_join(r, root, file), bytes, n);
    }
    scenario_command_result_t result = command(r,
                                               c,
                                               ARGS(c->host,
                                                    language_join(r, root, "main.goat"),
                                                    language_join(r, root, "config.json"),
                                                    "required"),
                                               15000);
    yyjson_val *status = yyjson_obj_get(record, "status");
    REQUIRE(yyjson_is_int(status));
    REQUIRE(result.exit_code == yyjson_get_sint(status));
    REQUIRE(utf8(result.out, result.out_size));
    const char *expected = field(r, record, "expected");
    scenario_require_bytes(r,
                           "native exit stdout",
                           expected,
                           strlen(expected),
                           result.out,
                           result.out_size,
                           true);
    if (terminal)
        REQUIRE(has(result.err, result.err_size, old ? "METRICS " : "native adapter failed"));
    else if (yyjson_get_sint(status) == 0 && !exited)
        REQUIRE(has(result.err, result.err_size, "METRICS "));
    else
        REQUIRE(result.err_size == 0);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
    int result = 1;
    if (argc == 8 || (argc == 9 && scenario_keep_work_argument(argv[8]))) {
        fs_error_t e = {0};
        char *host = canonical_fs_path(argv[2], &e);
        clear_fs_error(&e);
        char *repository = absolute_fs_path(argv[4], &e);
        clear_fs_error(&e);
        char *cwd = absolute_fs_path(".", &e);
        clear_fs_error(&e);
        char *path = join_fs_path(argv[7], "scenarios.json", &e);
        clear_fs_error(&e);
        void *bytes = NULL;
        size_t size = 0;
        bool read = path && read_fs_regular_file(path, 1024 * 1024, &bytes, &size, &e);
        clear_fs_error(&e);
        yyjson_read_err error = {0};
        yyjson_doc *doc = read ? yyjson_read_opts(bytes, size, 0, NULL, &error) : NULL;
        yyjson_val *records = yyjson_doc_get_root(doc);
        size_t count = yyjson_arr_size(records);
        if (host && repository && cwd && yyjson_is_arr(records) && count == 27) {
            config_t *configs = CALLOC((count + 1) * sizeof(*configs));
            scenario_definition_t *definitions = CALLOC((count + 1) * sizeof(*definitions));
            configs[0] = (config_t){host, argv[3], repository, argv[7], cwd, NULL};
            definitions[0] = (scenario_definition_t){"integration.native_exit.exit-prefixes",
                                                     90000,
                                                     prefix,
                                                     configs,
                                                     NULL};
            for (size_t i = 0; i < count; i++) {
                yyjson_val *record = yyjson_arr_get(records, i);
                configs[i + 1] = (config_t){host, argv[3], repository, argv[7], cwd, record};
                definitions[i + 1] =
                    (scenario_definition_t){yyjson_get_str(yyjson_obj_get(record, "id")),
                                            90000,
                                            body,
                                            configs + i + 1,
                                            NULL};
            }
            scenario_options_t options = {.build_directory = argv[5], .data_directory = argv[6]};
            const char *selection[] = {argv[1], argc == 9 ? argv[8] : NULL};
            result = scenario_registry_main(definitions,
                                            count + 1,
                                            argc == 9 ? 2 : 1,
                                            selection,
                                            &options);
            FREE(definitions);
            FREE(configs);
        } else
            fprintf(stderr,
                    "Cannot load native-exit fixtures: %s\n",
                    error.msg ? error.msg : "invalid input");
        yyjson_doc_free(doc);
        FREE(bytes);
        FREE(path);
        FREE(cwd);
        FREE(repository);
        FREE(host);
    } else
        fputs("usage: native_exit_scenarios ID HOST CC REPOSITORY BUILD DATA FIXTURES "
              "[--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
