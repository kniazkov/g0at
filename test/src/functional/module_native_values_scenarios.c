/** @file module_native_values_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real same-bitness module ABI values and unsafe-character VM fallback (#382).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <stdarg.h>
#include <stdlib.h>
#ifdef _WIN32
#    include <windows.h>
#    define SEP ';'
#else
#    include <unistd.h>
#    define SEP ':'
#endif
#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})

typedef struct {
    const unsigned char *data;
    size_t size;
} bytes_t;

typedef struct {
    const char *builder, *runner, *compiler, *cwd, *fixtures, *kind;
} config_t;

typedef struct {
    child_options_t options;
    scenario_command_result_t result;
} command_t;

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

static const char *canonical(scenario_run_t *r, const char *p) {
    fs_error_t e = {0};
    char *q = absolute_fs_path(p, &e);
    clear_fs_error(&e);
    REQUIRE(q);
    return language_owned(r, q);
}

static bool executable(scenario_run_t *r, const char *path) {
    fs_info_t info;
    fs_error_t error = {0};
    bool ok = inspect_fs_path(path, true, &info, &error);
    clear_fs_error(&error);
    if (!ok || info.kind != FS_FILE)
        return false;
#ifndef _WIN32
    return access(path, X_OK) == 0;
#else
    return true;
#endif
}

static const char *candidate(scenario_run_t *r, const char *path) {
    if (executable(r, path))
        return canonical(r, path);
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
                return canonical(r, extended);
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

static bool free_malloc(void *p) {
    free(p);
    return true;
}

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bytes_t read_bytes(scenario_run_t *r, const char *path) {
    size_t n;
    const char *p = language_read(r, path, &n);
    return (bytes_t){(const unsigned char *)p, n};
}

static const char *field(scenario_run_t *r, yyjson_val *obj, const char *name) {
    yyjson_val *v = yyjson_obj_get(obj, name);
    const char *s = yyjson_get_str(v);
    REQUIRE(s && strlen(s) == yyjson_get_len(v));
    return s;
}

static bytes_t part(scenario_run_t *r, bytes_t archive, const char *name) {
    format_zip_info_t info;
    char error[256];
    REQUIRE(format_zip_inspect(archive.data, archive.size, SIZE_MAX, &info, error, sizeof(error)));
    size_t n = info.count, found = SIZE_MAX;
    for (size_t i = 0; i < n; i++) {
        REQUIRE(format_zip_inspect(archive.data, archive.size, i, &info, error, sizeof(error)));
        if (info.name_size == strlen(name) && !memcmp(info.name, name, info.name_size))
            found = i;
    }
    REQUIRE(found != SIZE_MAX);
    size_t size;
    void *out = format_zip_extract(archive.data,
                                   archive.size,
                                   found,
                                   128 * 1024 * 1024,
                                   &size,
                                   error,
                                   sizeof(error));
    REQUIRE(out);
    scenario_defer(r, free_malloc, out);
    return (bytes_t){out, size};
}

static yyjson_val *parse(scenario_run_t *r, bytes_t bytes) {
    yyjson_doc *doc = yyjson_read((const char *)bytes.data, bytes.size, 0);
    REQUIRE(doc);
    scenario_defer(r, free_doc, doc);
    return yyjson_doc_get_root(doc);
}

static const char *
copy_text(scenario_run_t *r, config_t *c, const char *fixture, const char *name) {
    bytes_t b = read_bytes(r, language_join(r, c->fixtures, fixture));
    const char *path = language_join(r, scenario_work_directory(scenario_run_context(r)), name);
#ifdef _WIN32
    unsigned char *text = language_owned(r, ALLOC(b.size * 2 + 1));
    size_t n = 0;
    for (size_t i = 0; i < b.size; i++) {
        if (b.data[i] == '\n')
            text[n++] = '\r';
        text[n++] = b.data[i];
    }
    language_write(r, path, text, n);
#else
    language_write(r, path, b.data, b.size);
#endif
    return path;
}

static void invoke(scenario_run_t *r, void *user) {
    command_t *c = user;
    c->result = scenario_command_capture(r, &c->options);
}

static scenario_command_result_t
command(scenario_run_t *r, config_t *c, const char *const *args, uint64_t ms) {
    command_t call = {.options = {.executable = args[0],
                                  .arguments = args,
                                  .working_directory = c->cwd,
                                  .empty_stdin = true}};
    scenario_with_timeout(r, ms, invoke, &call);
    return call.result;
}

static scenario_command_result_t
build(scenario_run_t *r, config_t *c, const char *descriptor, const char *archive) {
    return command(r,
                   c,
                   ARGS(c->builder,
                        "native-build",
                        descriptor,
                        archive,
                        "descriptor",
                        "descriptor",
                        c->compiler),
                   120000);
}

static bool byte_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static bool contains(bytes_t b, const char *s) {
    size_t n = strlen(s);
    if (n > b.size)
        return false;
    for (size_t i = 0; i <= b.size - n; i++)
        if (!memcmp(b.data + i, s, n))
            return true;
    return false;
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

static yyjson_val *native_variant(scenario_run_t *r, yyjson_val *metadata) {
    yyjson_val *variants = yyjson_obj_get(metadata, "native");
    REQUIRE(yyjson_is_arr(variants) && yyjson_arr_size(variants) > 0);
    return yyjson_arr_get(variants, 0);
}

static uint64_t function_id(scenario_run_t *r, yyjson_val *metadata, const char *name) {
    yyjson_val *v =
        yyjson_obj_get(yyjson_obj_get(yyjson_obj_get(metadata, "externals"), name), "functionId");
    REQUIRE(yyjson_is_uint(v));
    return yyjson_get_uint(v);
}

static bool included(scenario_run_t *r, yyjson_val *variant, uint64_t id) {
    yyjson_val *a = yyjson_obj_get(variant, "specializations");
    REQUIRE(yyjson_is_arr(a));
    size_t i, n;
    yyjson_val *item;
    bool found = false;
    yyjson_arr_foreach(a, i, n, item) {
        yyjson_val *v = yyjson_obj_get(item, "functionId");
        REQUIRE(yyjson_is_uint(v));
        found |= yyjson_get_uint(v) == id;
    }
    return found;
}

static void body(scenario_run_t *r, void *user) {
    config_t local = *(config_t *)user;
    config_t *c = &local;
    const char *compiler = find_tool(r, c->compiler);
    if (compiler)
        c->compiler = compiler;
    bool abi = !strcmp(c->kind, "abi-values");
    bool rejected = !strcmp(c->kind, "required-unsafe-only");
    const char *source =
        copy_text(r, c, abi ? "values.goat" : "unsafe-character.goat", "main.goat");
    const char *descriptor = copy_text(r, c, "module.gmod", "module.gmod");
    const char *archive = language_join(r,
                                        scenario_work_directory(scenario_run_context(r)),
                                        abi ? "module.gbin" : "unsafe-character.gbin");
    scenario_command_result_t result = build(r, c, descriptor, archive);
    REQUIRE(result.exit_code == 0);
    if (abi)
        REQUIRE(result.out_size == 0);
    if (rejected) {
        copy_text(r, c, "unsafe-only.goat", "main.goat");
        result = build(r, c, descriptor, archive);
        REQUIRE(result.exit_code != 0 && result.out_size == 0);
        REQUIRE(contains((bytes_t){result.err, result.err_size}, "own specialization"));
        return;
    }
    bytes_t bytes = read_bytes(r, archive), manifest = part(r, bytes, "module.json");
    yyjson_val *metadata = parse(r, manifest), *variant = native_variant(r, metadata);
    if (abi) {
        bytes_t library = part(r, bytes, field(r, variant, "library"));
#ifdef _WIN32
        const char *name = "values.dll";
#else
        const char *name = "values.so";
#endif
        const char *dll = language_join(r, scenario_work_directory(scenario_run_context(r)), name);
        const char *json =
            language_join(r, scenario_work_directory(scenario_run_context(r)), "module.json");
        language_write(r, dll, library.data, library.size);
        language_write(r, json, manifest.data, manifest.size);
        result = command(r, c, ARGS(c->runner, dll, json), 30000);
        REQUIRE(result.exit_code == 0);
        REQUIRE(utf8(result.out, result.out_size));
        /* Python printed decoded stdout.strip(); no output-content or stderr assertion. */
    } else {
        REQUIRE(included(r, variant, function_id(r, metadata, "square")));
        REQUIRE(!included(r, variant, function_id(r, metadata, "convert")));
        REQUIRE(!included(r, variant, function_id(r, metadata, "indirect")));
        result = command(r, c, ARGS(c->builder, "binary", archive), 30000);
        REQUIRE(result.exit_code == 0);
        const unsigned char *out = result.out;
        size_t begin = 0, end = result.out_size;
        while (begin < end && byte_space(out[begin]))
            begin++;
        while (end > begin && byte_space(out[end - 1]))
            end--;
        scenario_require_bytes(r,
                               "unsafe character VM stdout",
                               "4294967295",
                               10,
                               out + begin,
                               end - begin,
                               false);
    }
    (void)source;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
    int result = 1;
    if (argc == 8 || (argc == 9 && scenario_keep_work_argument(argv[8]))) {
        fs_error_t e = {0};
        char *builder = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        char *runner = absolute_fs_path(argv[3], &e);
        clear_fs_error(&e);
        char *cwd = absolute_fs_path(".", &e);
        clear_fs_error(&e);
        if (builder && runner && cwd) {
            const char *kinds[] = {"abi-values", "unsafe-character-vm", "required-unsafe-only"};
            config_t configs[3];
            scenario_definition_t defs[3];
            char ids[3][128];
            for (size_t i = 0; i < 3; i++) {
                snprintf(ids[i], sizeof(ids[i]), "integration.module_native_values.%s", kinds[i]);
                configs[i] = (config_t){builder, runner, argv[4], cwd, argv[7], kinds[i]};
                defs[i] = (scenario_definition_t){ids[i], 300000, body, configs + i, NULL};
            }
            scenario_options_t options = {.build_directory = argv[5], .data_directory = argv[6]};
            const char *selection[] = {argv[1], argc == 9 ? argv[8] : NULL};
            result = scenario_registry_main(defs, 3, argc == 9 ? 2 : 1, selection, &options);
        }
        FREE(builder);
        FREE(runner);
        FREE(cwd);
    } else
        fputs("usage: module_native_values_scenarios ID BUILDER RUNNER CC BUILD DATA FIXTURES "
              "[--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
