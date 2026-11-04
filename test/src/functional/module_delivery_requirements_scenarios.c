/** @file module_delivery_requirements_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real compiler flags, selector compatibility and exact runtime floors (#389).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#include "test/support/format_json.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#    include <unistd.h>
#endif

#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})
typedef yyjson_mut_val jval_t;

typedef struct {
    const void *data;
    size_t size;
} bytes_t;

typedef struct {
    const char *goat, *wrapper, *selector, *cc, *format, *id, *destination;
} config_t;

typedef struct {
    scenario_run_t *r;
    config_t *c;
    const char *root, *data;
    yyjson_mut_doc *schema;
    unsigned count;
} suite_t;

typedef struct {
    bytes_t raw;
    size_t count;
    format_zip_info_t *entries;
    bytes_t *parts;
    yyjson_mut_doc *doc;
} archive_t;

static jval_t *get(jval_t *v, const char *k) {
    return yyjson_mut_obj_get(v, k);
}

static bool free_plain(void *p) {
    free(p);
    return true;
}

static bool free_doc(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool free_entries(void *p) {
    clear_fs_entries(p);
    FREE(p);
    return true;
}

static const char *fmt(scenario_run_t *r, const char *format, ...) {
    va_list a, b;
    va_start(a, format);
    va_copy(b, a);
    int n = vsnprintf(NULL, 0, format, b);
    va_end(b);
    REQUIRE(n >= 0);
    char *s = language_owned(r, ALLOC((size_t)n + 1));
    vsnprintf(s, (size_t)n + 1, format, a);
    va_end(a);
    return s;
}

static const char *str(scenario_run_t *r, jval_t *v) {
    const char *s = yyjson_mut_get_str(v);
    REQUIRE(s && strlen(s) == yyjson_mut_get_len(v));
    return s;
}

static bool eq(jval_t *v, const char *s) {
    return yyjson_mut_is_str(v) && yyjson_mut_get_len(v) == strlen(s)
           && !memcmp(yyjson_mut_get_str(v), s, strlen(s));
}

static bytes_t read_bytes(scenario_run_t *r, const char *p) {
    size_t n;
    const char *d = language_read(r, p, &n);
    return (bytes_t){d, n};
}

static yyjson_mut_doc *parse(scenario_run_t *r, bytes_t b) {
    char error[512];
    yyjson_mut_doc *d = format_json_parse(b.data, b.size, error, sizeof(error));
    if (d)
        scenario_defer(r, free_doc, d);
    scenario_require(r, d != NULL, error);
    return d;
}

static bool exists(scenario_run_t *r, const char *p) {
    fs_info_t info;
    fs_error_t e = {0};
    bool ok = inspect_fs_path(p, false, &info, &e), absent = e.status == FS_NOT_FOUND;
    clear_fs_error(&e);
    REQUIRE(ok || absent);
    return ok;
}

static void directory(scenario_run_t *r, const char *p) {
    if (exists(r, p))
        return;
    const char *slash = strrchr(p, '/');
    if (slash && slash != p)
        directory(r, fmt(r, "%.*s", (int)(slash - p), p));
    language_directory(r, p);
}

static void write_bytes(scenario_run_t *r, const char *p, bytes_t b) {
    const char *slash = strrchr(p, '/');
    if (slash && slash != p)
        directory(r, fmt(r, "%.*s", (int)(slash - p), p));
    language_write(r, p, b.data, b.size);
}

static fs_entries_t *entries(scenario_run_t *r, const char *p) {
    fs_entries_t *list = CALLOC(sizeof(*list));
    scenario_defer(r, free_entries, list);
    fs_error_t e = {0};
    bool ok = list_fs_directory(p, list, &e);
    clear_fs_error(&e);
    REQUIRE(ok);
    return list;
}

/* Compose per-child environments without changing the runner's environment. */
static bool env_name(const char *value, const char *key) {
    size_t n = strlen(key);
#ifdef _WIN32
    return !_strnicmp(value, key, n) && value[n] == '=';
#else
    return !strncmp(value, key, n) && value[n] == '=';
#endif
}

static const char *const *
env_set(scenario_run_t *r, const char *const *base, const char *key, const char *value) {
    if (!base)
        return language_environment_override(r, key, value);
    size_t n = 0, k = 0;
    while (base[n])
        n++;
    const char **out = language_owned(r, CALLOC((n + 2) * sizeof(*out)));
    for (size_t i = 0; i < n; i++)
        if (!env_name(base[i], key))
            out[k++] = base[i];
    out[k] = fmt(r, "%s=%s", key, value);
    return out;
}

typedef struct {
    const char *exe, *cwd;
    const char *const *args, *const *env;
    scenario_command_result_t result;
} command_t;

static void command_body(scenario_run_t *r, void *p) {
    command_t *c = p;
    child_options_t options = {.executable = c->exe,
                               .working_directory = c->cwd,
                               .arguments = c->args,
                               .environment = c->env,
                               .empty_stdin = true};
    c->result = scenario_command_capture(r, &options);
}

static scenario_command_result_t invoke(suite_t *s,
                                        const char *exe,
                                        const char *const *args,
                                        const char *const *env,
                                        bool fail,
                                        const char *expected) {
    scenario_run_t *r = s->r;
    /* Child process arguments include argv[0], unlike invoke's argument list. */
    size_t count = 0;
    while (args[count])
        count++;
    const char **arguments = language_owned(r, CALLOC((count + 2) * sizeof(*arguments)));
    arguments[0] = exe;
    for (size_t i = 0; i < count; i++)
        arguments[i + 1] = args[i];
    command_t command = {exe, s->root, arguments, env, {0}};
    scenario_with_timeout(r, 180000, command_body, &command);
    scenario_command_result_t v = command.result;
    REQUIRE((v.exit_code != 0) == fail);
    if (fail)
        REQUIRE(v.out_size || v.err_size);
    if (expected)
        scenario_require_bytes(r,
                               "delivery stdout",
                               expected,
                               strlen(expected),
                               v.out,
                               v.out_size,
                               true);
    s->count++;
    return v;
}

static archive_t *archive(suite_t *s, const char *path, bool deflated) {
    scenario_run_t *r = s->r;
    archive_t *a = language_owned(r, CALLOC(sizeof(*a)));
    a->raw = read_bytes(r, path);
    format_zip_info_t info;
    char error[512];
    REQUIRE(format_zip_inspect(a->raw.data, a->raw.size, SIZE_MAX, &info, error, sizeof(error)));
    a->count = info.count;
    REQUIRE(a->count >= 2 && a->count <= 4096);
    a->entries = language_owned(r, CALLOC(a->count * sizeof(*a->entries)));
    a->parts = language_owned(r, CALLOC(a->count * sizeof(*a->parts)));
    for (size_t i = 0; i < a->count; i++) {
        REQUIRE(
            format_zip_inspect(a->raw.data, a->raw.size, i, a->entries + i, error, sizeof(error)));
        if (deflated)
            REQUIRE(a->entries[i].method == 8);
        size_t n;
        void *p = format_zip_extract(a->raw.data,
                                     a->raw.size,
                                     i,
                                     32 * 1024 * 1024,
                                     &n,
                                     error,
                                     sizeof(error));
        if (p)
            scenario_defer(r, free_plain, p);
        REQUIRE(p);
        a->parts[i] = (bytes_t){p, n};
        if (a->entries[i].name_size == 11 && !memcmp(a->entries[i].name, "module.json", 11))
            a->doc = parse(r, a->parts[i]);
    }
    REQUIRE(a->doc);
    return a;
}

static size_t member(scenario_run_t *r, archive_t *a, const char *name) {
    for (size_t i = 0; i < a->count; i++)
        if (a->entries[i].name_size == strlen(name)
            && !memcmp(a->entries[i].name, name, strlen(name)))
            return i;
    REQUIRE(false);
    return 0;
}

static jval_t *doc(archive_t *a) {
    return yyjson_mut_doc_get_root(a->doc);
}

static void valid_archive(suite_t *s, archive_t *a) {
    scenario_run_t *r = s->r;
    bytes_t code = a->parts[member(r, a, "bytecode.bin")];
    char hash[65], error[512];
    REQUIRE(format_binary_main_hash(doc(a),
                                    yyjson_mut_doc_get_root(s->schema),
                                    code.data,
                                    code.size,
                                    hash,
                                    error,
                                    sizeof(error)));
    REQUIRE(eq(get(doc(a), "mainHash"), hash));
    jval_t *native = get(doc(a), "native");
    for (size_t i = 0; i < yyjson_mut_arr_size(native); i++) {
        jval_t *v = yyjson_mut_arr_get(native, i);
        bytes_t library = a->parts[member(r, a, str(r, get(v, "library")))];
        format_binary_sha256(library.data, library.size, hash);
        REQUIRE(eq(get(v, "hash"), hash));
    }
}

static archive_t *contents(suite_t *s, const char *path, bool deflated) {
    archive_t *a = archive(s, path, deflated);
    valid_archive(s, a);
    return a;
}

static jval_t *target(scenario_run_t *r, archive_t *a) {
    jval_t *n = get(doc(a), "native");
    REQUIRE(yyjson_mut_is_arr(n) && yyjson_mut_arr_size(n) == 1);
    return get(yyjson_mut_arr_get(n, 0), "target");
}

static jval_t *copy(scenario_run_t *r, yyjson_mut_doc *d, jval_t *v) {
    jval_t *out = yyjson_mut_val_mut_copy(d, v);
    REQUIRE(out);
    return out;
}

static void set(scenario_run_t *r, yyjson_mut_doc *d, jval_t *v, const char *key, jval_t *value) {
    REQUIRE(value && yyjson_mut_obj_put(v, yyjson_mut_str(d, key), value));
}

static jval_t *strings(scenario_run_t *r, yyjson_mut_doc *d, const char *a, const char *b) {
    jval_t *out = yyjson_mut_arr(d);
    REQUIRE(out);
    if (a)
        REQUIRE(yyjson_mut_arr_add_str(d, out, a));
    if (b)
        REQUIRE(yyjson_mut_arr_add_str(d, out, b));
    return out;
}

static bool unsigned_integer(jval_t *v, uint64_t *value) {
    if (yyjson_mut_is_sint(v)) {
        int64_t n = yyjson_mut_get_sint(v);
        if (n < 0)
            return false;
        *value = (uint64_t)n;
        return true;
    }
    if (!yyjson_mut_is_uint(v))
        return false;
    *value = yyjson_mut_get_uint(v);
    return true;
}

static bool parse_version(jval_t *v, uint32_t out[4]) {
    if (!yyjson_mut_is_arr(v) || !yyjson_mut_arr_size(v) || yyjson_mut_arr_size(v) > 4)
        return false;
    memset(out, 0, 4 * sizeof(*out));
    for (size_t i = 0; i < yyjson_mut_arr_size(v); i++) {
        uint64_t n;
        if (!unsigned_integer(yyjson_mut_arr_get(v, i), &n) || n > UINT32_MAX)
            return false;
        out[i] = (uint32_t)n;
    }
    return true;
}

static void version(scenario_run_t *r, jval_t *v, uint32_t out[4]) {
    REQUIRE(parse_version(v, out));
}

static bool floor_target(jval_t *t) {
    uint32_t v[4];
    return eq(get(t, "os"), "linux") && eq(get(get(t, "runtime"), "family"), "glibc")
           && parse_version(get(get(t, "runtime"), "minimumVersion"), v);
}

static int version_compare(const uint32_t a[4], const uint32_t b[4]) {
    for (size_t i = 0; i < 4; i++)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}

static const char *known_runtime_image(const uint32_t x[4]) {
    if (x[0] != 2 || (x[1] != 35 && x[1] != 36) || x[2] || x[3])
        return NULL;
    return x[1] == 35 ? "ubuntu:22.04" : "debian:bookworm-slim";
}

static const char *runtime_image(scenario_run_t *r, jval_t *v) {
    uint32_t x[4];
    version(r, v, x);
    const char *image = known_runtime_image(x);
    REQUIRE(image);
    return image;
}

static const char *glibc_version(scenario_run_t *r, uint32_t out[4]) {
#if defined(__GLIBC__)
    size_t n = confstr(_CS_GNU_LIBC_VERSION, NULL, 0);
    REQUIRE(n > 7);
    char *text = language_owned(r, ALLOC(n));
    REQUIRE(confstr(_CS_GNU_LIBC_VERSION, text, n) == n && !strncmp(text, "glibc ", 6));
    char tail;
    memset(out, 0, 4 * sizeof(*out));
    REQUIRE(sscanf(text + 6, "%u.%u%c", out, out + 1, &tail) == 2);
    return text + 6;
#else
    (void)out;
    REQUIRE(false);
    return NULL;
#endif
}

static const char *fixture(suite_t *s, const char *name) {
    return language_join(s->r, s->data, name);
}

static const char *source(suite_t *s, bool floor) {
    scenario_run_t *r = s->r;
    write_bytes(r, language_join(r, s->root, "main.goat"), read_bytes(r, fixture(s, "main.goat")));
    const char *p = language_join(r, s->root, "module.gmod");
    write_bytes(r, p, read_bytes(r, fixture(s, floor ? "floor.gmod" : "audit.gmod")));
    return p;
}

static const char *build(suite_t *s,
                         const char *descriptor,
                         const char *name,
                         const char *subdir,
                         const char *compiler,
                         const char *native_target,
                         const char *const *env) {
    scenario_run_t *r = s->r;
    const char *out = language_join(r, s->root, subdir);
    invoke(s,
           s->c->goat,
           ARGS("build",
                descriptor,
                "--native",
                "required",
                "--cc",
                compiler,
                "--module-dir",
                out,
                "--native-target",
                native_target),
           env,
           false,
           "");
    const char *path = language_join(r, out, fmt(r, "%s/1.gbin", name));
    contents(s, path, false);
    return path;
}

static void destination(suite_t *s) {
    scenario_run_t *r = s->r;
    REQUIRE(s->c->destination);
    directory(r, s->c->destination);
    REQUIRE(entries(r, s->c->destination)->count == 0);
}

static void persist(suite_t *s, const char *name, const char *path) {
    if (s->c->destination)
        write_bytes(s->r, language_join(s->r, s->c->destination, name), read_bytes(s->r, path));
}

/* The recorder is a binary argv stream: NUL-delimited words, LF-delimited invocations. */
typedef struct {
    const char **words;
    size_t count;
} command_line_t;

typedef struct {
    command_line_t *lines;
    size_t count;
} command_log_t;

static command_log_t command_log(scenario_run_t *r, bytes_t input) {
    command_log_t result = {0};
    result.lines = language_owned(r, CALLOC((input.size + 1) * sizeof(*result.lines)));
    const char *raw = input.data;
    size_t begin = 0;
    for (size_t end = 0; end < input.size; end++) {
        if (raw[end] != '\n')
            continue;
        command_line_t *line = result.lines + result.count++;
        line->words = language_owned(r, CALLOC((end - begin + 2) * sizeof(*line->words)));
        size_t start = begin;
        for (size_t j = begin; j <= end; j++) {
            if (j != end && raw[j])
                continue;
            if (j > start)
                line->words[line->count++] = fmt(r, "%.*s", (int)(j - start), raw + start);
            start = j + 1;
        }
        begin = end + 1;
    }
    REQUIRE(begin == input.size);
    return result;
}

static bool has(command_line_t *line, const char *word) {
    for (size_t i = 0; i < line->count; i++)
        if (!strcmp(line->words[i], word))
            return true;
    return false;
}

static void embedded_runtime(scenario_run_t *r, command_line_t *line) {
    const char *names[] = {"runtime_strings.c",
                           "runtime_str.c",
                           "runtime_number_format.c",
                           "runtime_unicode.c"};
    for (size_t i = 0; i < 4; i++)
        REQUIRE(has(line, names[i]));
}

static scenario_command_result_t macros(suite_t *s, command_line_t *line) {
    scenario_run_t *r = s->r;
    size_t n = 0;
    while (n < line->count && strcmp(line->words[n], "-o"))
        n++;
    REQUIRE(n < line->count);
    const char **args = language_owned(r, CALLOC((n + 7) * sizeof(*args)));
    for (size_t i = 0; i < n; i++)
        args[i] = line->words[i];
    args[n++] = "-dM";
    args[n++] = "-E";
    args[n++] = "-x";
    args[n++] = "c";
    args[n] = "-";
    return invoke(s, s->c->cc, args, NULL, false, NULL);
}

static bool contains(bytes_t b, const char *needle) {
    size_t n = strlen(needle);
    for (size_t i = 0; n <= b.size && i <= b.size - n; i++)
        if (!memcmp((const char *)b.data + i, needle, n))
            return true;
    return false;
}

static void
macro_present(scenario_run_t *r, scenario_command_result_t v, const char *name, bool present) {
    REQUIRE(
        contains((bytes_t){v.out, v.out_size}, fmt(r, "#define %s %s", name, present ? "1" : ""))
        == present);
}

static void le32(unsigned char *p, uint32_t value) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (unsigned char)(value >> (i * 8));
}

typedef struct {
    child_options_t options;
    scenario_command_result_t result;
} binary_command_t;

static void binary_command_body(scenario_run_t *r, void *user) {
    binary_command_t *c = user;
    c->result = scenario_command_capture(r, &c->options);
}

static void selected(suite_t *s,
                     archive_t *a,
                     jval_t *profile,
                     int expected,
                     const uint32_t *abis,
                     size_t count) {
    scenario_run_t *r = s->r;
    size_t n;
    char *json = format_json_canonical(profile, &n);
    if (json)
        scenario_defer(r, free_plain, json);
    REQUIRE(json && n <= UINT32_MAX && count <= UINT32_MAX);
    size_t size = n + 8 + 4 * count + a->raw.size;
    unsigned char *frame = language_owned(r, ALLOC(size));
    le32(frame, (uint32_t)n);
    memcpy(frame + 4, json, n);
    le32(frame + 4 + n, (uint32_t)count);
    for (size_t i = 0; i < count; i++)
        le32(frame + n + 8 + i * 4, abis[i]);
    memcpy(frame + n + 8 + count * 4, a->raw.data, a->raw.size);
    child_options_t options = {0};
    options.executable = s->c->selector;
    options.arguments = ARGS(s->c->selector, "select");
    options.stdin_data = frame;
    options.stdin_size = size;
    binary_command_t command = {.options = options};
    scenario_with_timeout(r, 180000, binary_command_body, &command);
    scenario_command_result_t result = command.result;
    REQUIRE(result.exit_code == 0);
    yyjson_mut_doc *report = parse(r, (bytes_t){result.out, result.out_size});
    jval_t *status = get(yyjson_mut_doc_get_root(report), "status");
    uint64_t value;
    REQUIRE(unsigned_integer(status, &value) && value == (unsigned)expected);
    s->count++;
}

static void select_one(suite_t *s, archive_t *a, jval_t *profile, int expected) {
    uint32_t abi = 1;
    selected(s, a, profile, expected, &abi, 1);
}

/* Shared with the temporary original-artifact parity replay. */
static unsigned selector_matrix(suite_t *s, archive_t *a) {
    scenario_run_t *r = s->r;
    jval_t *t = target(r, a), *arch = get(t, "architecture");
    bool x64 = eq(arch, "x86_64"), arm = eq(arch, "aarch64");
    REQUIRE(x64 || arm || eq(arch, "x86"));
    unsigned before = s->count;
    select_one(s, a, t, 0);
    uint32_t abis[] = {0, 2, UINT32_MAX};
    selected(s, a, t, 3, NULL, 0);
    for (size_t i = 0; i < 3; i++)
        selected(s, a, t, 3, abis + i, 1);
    jval_t *p = copy(r, a->doc, t);
    set(r, a->doc, p, "cpuFeatures", strings(r, a->doc, NULL, NULL));
    select_one(s, a, p, 3);
    if (x64 || arm) {
        p = copy(r, a->doc, t);
        set(r, a->doc, p, "architecture", yyjson_mut_str(a->doc, x64 ? "aarch64" : "x86_64"));
        set(r,
            a->doc,
            p,
            "cpuFeatures",
            strings(r, a->doc, x64 ? "fp" : "sse2", x64 ? "neon" : NULL));
        select_one(s, a, p, 3);
    }
    p = copy(r, a->doc, t);
    if (eq(get(t, "os"), "linux")) {
        set(r, a->doc, get(p, "runtime"), "family", yyjson_mut_str(a->doc, "musl"));
        select_one(s, a, p, 3);
    } else {
        REQUIRE(eq(get(t, "os"), "windows"));
        set(r, a->doc, p, "architecture", yyjson_mut_str(a->doc, x64 ? "x86" : "x86_64"));
        set(r, a->doc, p, "bits", yyjson_mut_uint(a->doc, x64 ? 32 : 64));
        set(r, a->doc, p, "cpuFeatures", strings(r, a->doc, x64 ? "i686" : "sse2", NULL));
        select_one(s, a, p, 3);
        if (x64) {
            p = copy(r, a->doc, t);
            set(r,
                a->doc,
                get(p, "runtime"),
                "family",
                yyjson_mut_str(a->doc,
                               eq(get(get(t, "runtime"), "family"), "msvcrt") ? "ucrt" : "msvcrt"));
            select_one(s, a, p, 3);
        }
    }
    for (unsigned field = 0; field < 2; field++) {
        jval_t *minimum = field ? get(get(t, "runtime"), "minimumVersion") : get(t, "minimumOs");
        uint32_t v[4];
        version(r, minimum, v);
        size_t n = yyjson_mut_arr_size(minimum), nonzero = n;
        while (nonzero && !v[nonzero - 1])
            nonzero--;
        for (int increment = -1; increment <= 1; increment++) {
            if (increment < 0 && !nonzero)
                continue;
            uint32_t value[4];
            memcpy(value, v, sizeof(value));
            size_t index = increment < 0 ? nonzero - 1 : n - 1;
            REQUIRE(increment <= 0 || value[index] < UINT32_MAX);
            value[index] = (uint32_t)((int64_t)value[index] + increment);
            if (!field && eq(get(t, "os"), "windows") && value[0] < 10)
                continue;
            jval_t *arr = yyjson_mut_arr(a->doc);
            for (size_t i = 0; i < n; i++)
                REQUIRE(yyjson_mut_arr_add_uint(a->doc, arr, value[i]));
            p = copy(r, a->doc, t);
            set(r,
                a->doc,
                field ? get(p, "runtime") : p,
                field ? "minimumVersion" : "minimumOs",
                arr);
            select_one(s, a, p, increment < 0 ? 3 : 0);
        }
    }
    return s->count - before;
}

static void audit(suite_t *s) {
    scenario_run_t *r = s->r;
    if (s->c->destination)
        destination(s);
    const char *descriptor = source(s, false), *log = language_join(r, s->root, "arguments.log");
    const char *const *env = env_set(r, NULL, "GOAT_TOOLCHAIN_REAL", s->c->cc);
    env = env_set(r, env, "GOAT_TOOLCHAIN_ARGUMENTS_LOG", log);
    env = env_set(r, env, "CFLAGS", "-march=native -mavx512f");
    env = env_set(r, env, "CXXFLAGS", "-mcpu=native");
    const char *path = build(s, descriptor, "requirements", "baseline", s->c->wrapper, "host", env);
    archive_t *a = contents(s, path, false);
    jval_t *t = target(r, a), *arch = get(t, "architecture");
    bool x64 = eq(arch, "x86_64"), arm = eq(arch, "aarch64");
    REQUIRE(x64 || arm || eq(arch, "x86"));
    const char *baseline = x64 ? "-march=x86-64" : arm ? "-march=armv8-a" : "-march=i686";
    REQUIRE(format_json_equal(get(t, "cpuFeatures"),
                              strings(r,
                                      a->doc,
                                      x64   ? "sse2"
                                      : arm ? "fp"
                                            : "i686",
                                      arm ? "neon" : NULL)));
    command_log_t commands = command_log(r, read_bytes(r, log));
    command_line_t *first = NULL;
    for (size_t i = 0; i < commands.count; i++) {
        command_line_t *line = commands.lines + i;
        if (!has(line, "./module.c"))
            continue;
        if (!first)
            first = line;
        REQUIRE(has(line, baseline) && has(line, "-O2"));
        for (size_t j = 0; j < line->count; j++) {
            const char *f = line->words[j];
            REQUIRE((strncmp(f, "-march=", 7) && strncmp(f, "-mcpu=", 6)) || !strstr(f, "native"));
            REQUIRE(strncmp(f, "-mavx", 5) && strncmp(f, "-msve", 5));
        }
        embedded_runtime(r, line);
    }
    REQUIRE(first);
    scenario_command_result_t m = macros(s, first);
    macro_present(r, m, x64 ? "__SSE2__" : arm ? "__ARM_NEON" : "__i386__", true);
    const char *x86_forbidden[] = {"__AVX__", "__AVX2__", "__AVX512F__"};
    const char *arm_forbidden[] = {"__ARM_FEATURE_SVE",
                                   "__ARM_FEATURE_SVE2",
                                   "__ARM_FEATURE_CRYPTO",
                                   "__ARM_FEATURE_ATOMICS",
                                   "__ARM_FEATURE_AES",
                                   "__ARM_FEATURE_SHA2"};
    for (size_t i = 0; i < (arm ? 6u : 3u); i++)
        macro_present(r, m, arm ? arm_forbidden[i] : x86_forbidden[i], false);
    unsigned checks = selector_matrix(s, a);
    scenario_command_result_t host = invoke(s, s->c->selector, ARGS("host"), NULL, false, NULL);
    const char *fake[] = {"MINGW32", "MINGW64", "UCRT64"};
    for (size_t i = 0; i < 3; i++) {
        const char *const *fake_env =
            env_set(r, env_set(r, NULL, "MSYSTEM", fake[i]), "PATH", s->root);
        scenario_command_result_t v =
            invoke(s, s->c->selector, ARGS("host"), fake_env, false, NULL);
        scenario_require_bytes(r,
                               "host unaffected by MSYSTEM",
                               host.out,
                               host.out_size,
                               v.out,
                               v.out_size,
                               false);
    }
    persist(s, "baseline.gbin", path);
    if (x64 || arm) {
        const char *extension = x64 ? "avx2" : "sha2";
        const char *ext_path = build(s,
                                     descriptor,
                                     "requirements",
                                     "extended",
                                     s->c->wrapper,
                                     fmt(r, "host+%s", extension),
                                     env);
        archive_t *extended = contents(s, ext_path, false);
        jval_t *features = get(target(r, extended), "cpuFeatures");
        bool first_feature = false, second_feature = x64;
        for (size_t i = 0; i < yyjson_mut_arr_size(features); i++) {
            first_feature |= eq(yyjson_mut_arr_get(features, i), x64 ? "avx2" : "sha1");
            second_feature |= eq(yyjson_mut_arr_get(features, i), "sha2");
        }
        REQUIRE(first_feature && second_feature);
        commands = command_log(r, read_bytes(r, log));
        size_t found = 0;
        for (size_t i = 0; i < commands.count; i++) {
            command_line_t *line = commands.lines + i;
            if (!has(line, "./module.c") || !has(line, x64 ? "-mavx2" : "-march=armv8-a+sha2"))
                continue;
            found++;
            embedded_runtime(r, line);
            m = macros(s, line);
            macro_present(r, m, x64 ? "__AVX2__" : "__ARM_FEATURE_SHA2", true);
            if (arm) {
                macro_present(r, m, "__ARM_FEATURE_AES", false);
                macro_present(r, m, "__ARM_FEATURE_CRYPTO", false);
            }
        }
        REQUIRE(found);
        select_one(s, extended, t, 3);
        checks++;
        persist(s, "extended.gbin", ext_path);
    }
    printf("Delivery requirements: %u real-archive selection checks; effective compiler/runtime "
           "flags audited\n",
           checks);
}

/* Temporary #389 gate: no rebuilding, substituting, or patching producer bytes.
 * Remove this entry with the temporary Python parity gate after all remote checks pass. */
static void replay_audit(suite_t *s) {
    scenario_run_t *r = s->r;
    REQUIRE(s->c->destination);
    archive_t *a = contents(s, language_join(r, s->c->destination, "baseline.gbin"), false);
    unsigned count = selector_matrix(s, a);
    jval_t *t = target(r, a), *arch = get(t, "architecture");
    if (eq(arch, "x86_64") || eq(arch, "aarch64")) {
        archive_t *extended =
            contents(s, language_join(r, s->c->destination, "extended.gbin"), false);
        select_one(s, extended, t, 3);
        count++;
    }
    printf("Original artifact replay: %u real-archive selection checks passed\n", count);
}

static void print_version(jval_t *v) {
    putchar('[');
    for (size_t i = 0; i < yyjson_mut_arr_size(v); i++)
        printf("%s%u", i ? ", " : "", (unsigned)yyjson_mut_get_uint(yyjson_mut_arr_get(v, i)));
    putchar(']');
}

static void prepare(suite_t *s) {
    scenario_run_t *r = s->r;
    uint32_t host[4], floor[4] = {2, 35, 0, 0};
    glibc_version(r, host);
    REQUIRE(version_compare(host, floor) > 0);
    destination(s);
    const char *descriptor = source(s, true);
    const char *ordinary_path =
        build(s, descriptor, "runtime-floor", "ordinary", s->c->cc, "host", NULL);
    archive_t *ordinary = contents(s, ordinary_path, false);
    jval_t *ordinary_minimum = get(get(target(r, ordinary), "runtime"), "minimumVersion");
    const char *extra = language_join(r, s->root, "runtime_floor.c");
    write_bytes(r, extra, read_bytes(r, fixture(s, "runtime_floor.c.txt")));
    const char *log = language_join(r, s->root, "control-arguments.log");
    const char *const *env = env_set(r, NULL, "GOAT_TOOLCHAIN_REAL", s->c->cc);
    env = env_set(r, env, "GOAT_TOOLCHAIN_EXTRA_SOURCE", extra);
    env = env_set(r, env, "GOAT_TOOLCHAIN_ARGUMENTS_LOG", log);
    const char *path = build(s, descriptor, "runtime-floor", "control", s->c->wrapper, "host", env);
    command_log_t commands = command_log(r, read_bytes(r, log));
    size_t found = 0;
    for (size_t i = 0; i < commands.count; i++) {
        command_line_t *line = commands.lines + i;
        if (!has(line, "./module.c"))
            continue;
        found++;
        REQUIRE(has(line, extra));
        embedded_runtime(r, line);
    }
    REQUIRE(found);
    archive_t *a = contents(s, path, false);
    REQUIRE(format_json_equal(get(doc(a), "mainHash"), get(doc(ordinary), "mainHash")));
    bytes_t code = a->parts[member(r, a, "bytecode.bin")],
            ordinary_code = ordinary->parts[member(r, ordinary, "bytecode.bin")];
    scenario_require_bytes(r,
                           "ordinary/control bytecode",
                           ordinary_code.data,
                           ordinary_code.size,
                           code.data,
                           code.size,
                           false);
    jval_t *t = target(r, a), *minimum = get(get(t, "runtime"), "minimumVersion");
    REQUIRE(floor_target(t));
    uint32_t expected[4], actual[4];
    version(r, ordinary_minimum, expected);
    version(r, minimum, actual);
    if (version_compare(expected, floor) < 0)
        memcpy(expected, floor, sizeof(expected));
    REQUIRE(version_compare(expected, actual) == 0 && version_compare(host, expected) > 0);
    runtime_image(r, minimum);
    jval_t *variant = yyjson_mut_arr_get(get(doc(a), "native"), 0);
    const char *library = language_join(r, s->root, "control.so");
    write_bytes(r, library, a->parts[member(r, a, str(r, get(variant, "library")))]);
    scenario_command_result_t symbols = invoke(s,
                                               language_find_executable(r, "readelf"),
                                               ARGS("--wide", "--dyn-syms", library),
                                               NULL,
                                               false,
                                               NULL);
    REQUIRE(contains((bytes_t){symbols.out, symbols.out_size}, "__memcmpeq@GLIBC_2.35"));
    const char *marker = language_join(r, s->root, "loaded.txt");
    invoke(s,
           s->c->goat,
           ARGS(path, "--native", "required"),
           env_set(r, NULL, "GOAT_FLOOR_MARKER", marker),
           false,
           "49\n");
    bytes_t loaded = read_bytes(r, marker);
    scenario_require_bytes(r,
                           "real floor constructor",
                           "memcmpeq-2.35\n",
                           14,
                           loaded.data,
                           loaded.size,
                           false);
    persist(s, "module.gbin", path);
    fs_entries_t *list = entries(r, s->c->destination);
    REQUIRE(list->count == 1 && !strcmp(list->names[0], "module.gbin"));
    fputs("Ordinary minimum ", stdout);
    print_version(ordinary_minimum);
    fputs("; control minimum ", stdout);
    print_version(minimum);
    puts(" including real __memcmpeq@GLIBC_2.35 dependency; metadata discovered by producer");
}

static bool parse_counter(bytes_t report, const char *name, unsigned long *value) {
    const char *data = report.data;
    size_t begin = 0, seen = 0, name_size = strlen(name);
    for (size_t end = 0; end <= report.size; end++) {
        if (end != report.size && data[end] != '\n')
            continue;
        size_t len = end - begin;
        if (len && data[begin + len - 1] == '\r')
            len--;
        if (len > name_size && !memcmp(data + begin, name, name_size)
            && data[begin + name_size] == '=') {
            size_t i = begin + name_size + 1;
            if (i == begin + len || seen++)
                return false;
            unsigned long n = 0;
            for (; i < begin + len; i++) {
                if (data[i] < '0' || data[i] > '9')
                    return false;
                unsigned digit = (unsigned)(data[i] - '0');
                if (n > (ULONG_MAX - digit) / 10)
                    return false;
                n = n * 10 + digit;
            }
            *value = n;
        }
        begin = end + 1;
    }
    return seen == 1;
}

static unsigned long counter(scenario_run_t *r, bytes_t report, const char *name) {
    unsigned long value = 0;
    REQUIRE(parse_counter(report, name, &value));
    return value;
}

static void verify(suite_t *s) {
    scenario_run_t *r = s->r;
    REQUIRE(s->c->destination);
    const char *path = language_join(r, s->c->destination, "module.gbin");
    archive_t *a = contents(s, path, false);
    jval_t *t = target(r, a);
    REQUIRE(floor_target(t));
    uint32_t actual[4], minimum[4];
    const char *actual_text = glibc_version(r, actual);
    version(r, get(get(t, "runtime"), "minimumVersion"), minimum);
    REQUIRE(version_compare(actual, minimum) == 0);
    const char *delivered = language_join(r, s->root, "module.gbin");
    write_bytes(r, delivered, a->raw);
    const char *const *env = env_set(r, NULL, "PATH", language_join(r, s->root, "no-compiler"));
    env = env_set(r, env, "CC", language_join(r, s->root, "absent"));
    env = env_set(r, env, "CXX", language_join(r, s->root, "absent"));
    env = env_set(r, env, "GOAT_MODULE_PATH", "");
    const char *modes[] = {"off", "auto", "required"};
    for (size_t i = 0; i < 3; i++) {
        const char *report = language_join(r, s->root, fmt(r, "%s.native.txt", modes[i]));
        const char *marker = language_join(r, s->root, fmt(r, "%s.loaded.txt", modes[i]));
        env = env_set(r, env, "GOAT_FLOOR_MARKER", marker);
        invoke(s,
               s->c->goat,
               ARGS(delivered, "--native", modes[i], "--save-native", report),
               env,
               false,
               "49\n");
        bytes_t counters = read_bytes(r, report);
        unsigned long succeeded = counter(r, counters, "succeeded");
        if (i) {
            REQUIRE(succeeded > 0 && counter(r, counters, "bound") > 0);
            bytes_t loaded = read_bytes(r, marker);
            scenario_require_bytes(r,
                                   "floor constructor",
                                   "memcmpeq-2.35\n",
                                   14,
                                   loaded.data,
                                   loaded.size,
                                   false);
        } else
            REQUIRE(succeeded == 0 && !exists(r, marker));
    }
    bytes_t after = read_bytes(r, delivered);
    scenario_require_bytes(r,
                           "immutable delivered archive",
                           a->raw.data,
                           a->raw.size,
                           after.data,
                           after.size,
                           false);
    after = read_bytes(r, path);
    scenario_require_bytes(r,
                           "immutable phase artifact",
                           a->raw.data,
                           a->raw.size,
                           after.data,
                           after.size,
                           false);
    fs_entries_t *list = entries(r, s->root);
    for (size_t i = 0; i < list->count; i++) {
        size_t n = strlen(list->names[i]);
        REQUIRE(n < 5
                || (strcmp(list->names[i] + n - 5, ".goat")
                    && strcmp(list->names[i] + n - 5, ".gmod")));
    }
    printf("Exact glibc %s floor: VM, auto and required passed; real native calls confirmed\n",
           actual_text);
}

static void controls(suite_t *s) {
    scenario_run_t *r = s->r;
    char error[512];
    format_zip_info_t info;
    REQUIRE(!format_zip_inspect((const unsigned char *)"bad",
                                3,
                                SIZE_MAX,
                                &info,
                                error,
                                sizeof(error)));
    format_zip_entry_t entry;
    format_zip_entry_init(&entry, "module.json", "{}", 2, false);
    size_t n;
    unsigned char *zip = format_zip_build(&entry, 1, NULL, 0, false, &n, error, sizeof(error));
    if (zip)
        scenario_defer(r, free_plain, zip);
    REQUIRE(zip);
    REQUIRE(format_zip_inspect(zip, n, 0, &info, error, sizeof(error)));
    size_t size;
    unsigned char *part = format_zip_extract(zip, n, 0, 1024, &size, error, sizeof(error));
    if (part)
        scenario_defer(r, free_plain, part);
    REQUIRE(part && size == 2);
    zip[info.data_offset] ^= 1;
    part = format_zip_extract(zip, n, 0, 1024, &size, error, sizeof(error));
    if (part)
        scenario_defer(r, free_plain, part);
    REQUIRE(!part);
    REQUIRE(!format_json_parse("{", 1, error, sizeof(error)));
    uint32_t a[4] = {2, 35}, b[4] = {2, 36};
    REQUIRE(version_compare(a, b) < 0 && version_compare(b, a) > 0 && version_compare(a, a) == 0);
    static const char control_log[] = "-O2\0./module.c\0-o\0module.so\0\n";
    command_log_t log = command_log(r, (bytes_t){control_log, sizeof(control_log) - 1});
    REQUIRE(log.count == 1 && has(log.lines, "./module.c") && has(log.lines, "-O2")
            && !has(log.lines, "-mavx2"));
    unsigned char word[4];
    le32(word, UINT32_C(0x12345678));
    REQUIRE(word[0] == 0x78 && word[1] == 0x56 && word[2] == 0x34 && word[3] == 0x12);
    const uint32_t unsupported[][4] = {{2, 34, 0, 0}, {2, 37, 0, 0}, {2, 35, 1, 0}, {2, 35, 0, 1}};
    for (size_t i = 0; i < 4; i++)
        REQUIRE(!known_runtime_image(unsupported[i]));
    REQUIRE(!strcmp(known_runtime_image(a), "ubuntu:22.04")
            && !strcmp(known_runtime_image(b), "debian:bookworm-slim"));
    const char *profiles[] = {
        "{\"os\":\"linux\",\"runtime\":{\"family\":\"glibc\",\"minimumVersion\":[2,35]}}",
        "{\"os\":\"linux\",\"runtime\":{\"family\":\"musl\",\"minimumVersion\":[2,35]}}",
        "{\"os\":\"windows\",\"runtime\":{\"family\":\"glibc\",\"minimumVersion\":[2,35]}}",
        "{\"os\":\"linux\",\"runtime\":{\"family\":\"glibc\",\"minimumVersion\":[-1]}}"};
    for (size_t i = 0; i < 4; i++) {
        yyjson_mut_doc *d = parse(r, (bytes_t){profiles[i], strlen(profiles[i])});
        REQUIRE(floor_target(yyjson_mut_doc_get_root(d)) == (i == 0));
        if (!i) {
            uint32_t v[4];
            version(r, get(get(yyjson_mut_doc_get_root(d), "runtime"), "minimumVersion"), v);
            REQUIRE(version_compare(v, a) == 0 && version_compare(v, b) != 0);
        }
    }
    const char *statuses[] = {"0", "3", "-1", "\"0\""};
    for (size_t i = 0; i < 4; i++) {
        yyjson_mut_doc *d = parse(r, (bytes_t){statuses[i], strlen(statuses[i])});
        uint64_t status;
        REQUIRE(unsigned_integer(yyjson_mut_doc_get_root(d), &status) == (i < 2));
        if (i < 2)
            REQUIRE(status == (i ? 3u : 0u));
    }
    unsigned long value;
    const char *bad_counters[] = {"succeeded=",
                                  "succeeded=-1",
                                  "succeeded=1x",
                                  "succeeded=1\nsucceeded=2\n",
                                  "succeeded=999999999999999999999999999999999999999",
                                  "bound=1"};
    for (size_t i = 0; i < sizeof(bad_counters) / sizeof(bad_counters[0]); i++)
        REQUIRE(!parse_counter((bytes_t){bad_counters[i], strlen(bad_counters[i])},
                               "succeeded",
                               &value));
    static const char good_counter[] = "succeeded=12\r\nbound=2\n";
    REQUIRE(parse_counter((bytes_t){good_counter, sizeof(good_counter) - 1}, "succeeded", &value)
            && value == 12);
    puts("Runtime requirements corruption and parser controls passed");
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
    suite_t s = {.r = r, .c = c};
    s.root =
        language_join(r, scenario_work_directory(scenario_run_context(r)), "requirements work");
    directory(r, s.root);
    s.data = language_join(r,
                           scenario_data_directory(scenario_run_context(r)),
                           "module-delivery-requirements");
    s.schema = parse(r, read_bytes(r, language_join(r, c->format, "schema-source.json")));
    if (!strcmp(c->id, "controls")) {
        controls(&s);
        return;
    }
    if (!strcmp(c->id, "audit") || !strcmp(c->id, "prepare-floor"))
        c->cc = language_find_executable(r, c->cc);
    if (!strcmp(c->id, "audit"))
        audit(&s);
    else if (!strcmp(c->id, "replay-audit"))
        replay_audit(&s);
    else if (!strcmp(c->id, "prepare-floor"))
        prepare(&s);
    else if (!strcmp(c->id, "verify-floor"))
        verify(&s);
    else {
        REQUIRE(c->destination);
        archive_t *a = contents(&s, language_join(r, c->destination, "module.gbin"), false);
        jval_t *t = target(r, a);
        REQUIRE(floor_target(t));
        puts(runtime_image(r, get(get(t, "runtime"), "minimumVersion")));
    }
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t argument_error = {0};
    if (!fs_arguments(&argc, &argv, &argument_error)) {
        clear_fs_error(&argument_error);
        return 1;
    }
#endif
    int result = 1;
    bool keep = false, timeout_set = false, valid = argc >= 9;
    unsigned timeout = 900;
    const char *destination_arg = NULL;
    for (int i = 9; valid && i < argc; i++) {
        if (!strcmp(argv[i], "--keep-work") && !keep)
            keep = true;
        else if (!strcmp(argv[i], "--destination") && !destination_arg && i + 1 < argc)
            destination_arg = argv[++i];
        else if (!strcmp(argv[i], "--timeout-seconds") && !timeout_set && i + 1 < argc) {
            timeout_set = true;
            timeout = 0;
            const char *p = argv[++i];
            valid = *p != 0;
            for (; valid && *p; p++) {
                if (*p < '0' || *p > '9' || timeout > 90)
                    valid = false;
                else {
                    timeout = timeout * 10 + (unsigned)(*p - '0');
                    valid = timeout <= 900;
                }
            }
            valid &= timeout > 30;
        } else
            valid = false;
    }
    if (valid) {
        char *paths[10] = {0};
        for (int i = 2; i < 9; i++) {
            fs_error_t e = {0};
            if (i == 5) {
                paths[i] = ALLOC(strlen(argv[i]) + 1);
                strcpy(paths[i], argv[i]);
            } else
                paths[i] = absolute_fs_path(argv[i], &e);
            clear_fs_error(&e);
            valid &= paths[i] != NULL;
        }
        if (destination_arg) {
            fs_error_t e = {0};
            paths[9] = absolute_fs_path(destination_arg, &e);
            clear_fs_error(&e);
            valid &= paths[9] != NULL;
        }
        if (valid) {
            const char *names[] = {"audit",
                                   "prepare-floor",
                                   "floor-image",
                                   "verify-floor",
                                   "controls",
                                   "replay-audit"};

            enum { COUNT = sizeof(names) / sizeof(names[0]) };

            config_t configs[COUNT];
            scenario_definition_t definitions[COUNT];
            char ids[COUNT][128];
            for (size_t i = 0; i < COUNT; i++) {
                snprintf(ids[i],
                         sizeof(ids[i]),
                         "integration.module_delivery_requirements.%s",
                         names[i]);
                configs[i] = (config_t){paths[2],
                                        paths[3],
                                        paths[4],
                                        paths[5],
                                        paths[8],
                                        names[i],
                                        paths[9]};
                definitions[i] =
                    (scenario_definition_t){ids[i], (timeout - 30) * 1000, body, configs + i, NULL};
            }
            scenario_options_t options = {.build_directory = paths[6], .data_directory = paths[7]};
            const char *selection[] = {argv[1], keep ? "--keep-work" : NULL};
            result = scenario_registry_main(definitions, COUNT, keep ? 2 : 1, selection, &options);
        }
        for (size_t i = 2; i < 10; i++)
            FREE(paths[i]);
    } else
        fputs("usage: module_delivery_requirements_scenarios ID GOAT WRAPPER SELECTOR CC BUILD "
              "DATA FORMAT_ROOT [--destination DIR] [--keep-work] [--timeout-seconds 31..900]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
