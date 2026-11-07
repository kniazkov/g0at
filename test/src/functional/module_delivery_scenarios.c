/** @file module_delivery_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent producer-to-compiler-free-consumer acceptance (#388).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#include "test/support/format_json.h"
#include "test/support/format_zip.h"
#include "test/support/inspect_report.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})
typedef yyjson_mut_val jval_t;

typedef struct {
    const void *data;
    size_t size;
} bytes_t;

typedef struct {
    const char *goat, *inspect, *merge, *cc, *format, *id;
} config_t;

typedef struct {
    scenario_run_t *r;
    config_t *c;
    const char *root, *source, *modules, *delivered, *data, *empty;
    const char *const *consumer;
    yyjson_mut_doc *schema, *report_schema;
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
    yyjson_mut_doc *d = inspect_report_parse(b.data, b.size, error, sizeof(error));
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

static void copy_tree(scenario_run_t *r, const char *from, const char *to) {
    fs_info_t info;
    fs_error_t e = {0};
    bool ok = inspect_fs_path(from, false, &info, &e);
    clear_fs_error(&e);
    REQUIRE(ok);
    if (info.kind == FS_FILE) {
        write_bytes(r, to, read_bytes(r, from));
        return;
    }
    REQUIRE(info.kind == FS_DIRECTORY);
    directory(r, to);
    fs_entries_t *list = entries(r, from);
    for (size_t i = 0; i < list->count; i++)
        copy_tree(r, language_join(r, from, list->names[i]), language_join(r, to, list->names[i]));
}

static void remove_tree(scenario_run_t *r, const char *p) {
    fs_error_t e = {0};
    bool ok = remove_fs_tree(p, &e);
    clear_fs_error(&e);
    REQUIRE(ok);
}

static void no_sources(scenario_run_t *r, const char *p) {
    fs_entries_t *list = entries(r, p);
    for (size_t i = 0; i < list->count; i++) {
        const char *name = list->names[i], *q = language_join(r, p, name);
        size_t n = strlen(name);
        REQUIRE(n < 5 || (strcmp(name + n - 5, ".goat") && strcmp(name + n - 5, ".gmod")));
        fs_info_t info;
        fs_error_t e = {0};
        bool ok = inspect_fs_path(q, false, &info, &e);
        clear_fs_error(&e);
        REQUIRE(ok);
        if (info.kind == FS_DIRECTORY)
            no_sources(r, q);
        else
            REQUIRE(info.kind == FS_FILE);
    }
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
#ifdef _WIN32
static const char *env_get(const char *const *env, const char *key) {
    for (size_t i = 0; env && env[i]; i++)
        if (env_name(env[i], key))
            return strchr(env[i], '=') + 1;
    return "";
}
#endif
static const char *const *producer_env(suite_t *s, const char *compiler) {
    scenario_run_t *r = s->r;
    const char *const *env = env_set(r, NULL, "GOAT_MODULE_PATH", s->modules);
#ifdef _WIN32
    fs_error_t e = {0};
    char *canonical = canonical_fs_path(compiler, &e);
    clear_fs_error(&e);
    language_owned(r, canonical);
    const char *slash = strrchr(canonical, '/');
    REQUIRE(slash);
    env = env_set(r,
                  env,
                  "PATH",
                  fmt(r, "%.*s;%s", (int)(slash - canonical), canonical, env_get(env, "PATH")));
#else
    (void)compiler;
#endif
    return env;
}

typedef struct {
    const char *exe;
    const char *const *args, *const *env;
    scenario_command_result_t result;
} command_t;

static void command_body(scenario_run_t *r, void *p) {
    command_t *c = p;
    c->result = language_command(r, c->exe, c->args, c->env);
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
    command_t command = {exe, arguments, env, {0}};
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

static const char *build(suite_t *s,
                         const char *name,
                         const char *version,
                         const char *source_file,
                         const char *mode,
                         const char *destination,
                         const char *compiler,
                         const char *target) {
    scenario_run_t *r = s->r;
    const char *dir = language_join(r, s->source, fmt(r, "%s-%s-%s", name, version, mode));
    directory(r, dir);
    write_bytes(r,
                language_join(r, dir, "main.goat"),
                read_bytes(r, language_join(r, s->data, source_file)));
    const char *descriptor = language_join(r, dir, "module.gmod");
    /* Descriptors are frozen separately from the implementation under test. */
    write_bytes(
        r,
        descriptor,
        read_bytes(r,
                   language_join(r,
                                 s->data,
                                 fmt(r, "descriptors/%s-%s-%s/module.gmod", name, version, mode))));
    invoke(s,
           s->c->goat,
           ARGS("build",
                descriptor,
                "--optimize",
                "none",
                "--module-dir",
                destination,
                "--native",
                mode,
                "--cc",
                compiler,
                "--native-target",
                target),
           producer_env(s, compiler),
           false,
           "");
    const char *out = language_join(r, destination, fmt(r, "%s/%s.gbin", name, version));
    contents(s, out, true);
    return out;
}

static void compare_identity(suite_t *s, archive_t *a, archive_t *b) {
    scenario_run_t *r = s->r;
    REQUIRE(format_json_equal(get(doc(a), "mainHash"), get(doc(b), "mainHash")));
    bytes_t x = a->parts[member(r, a, "bytecode.bin")], y = b->parts[member(r, b, "bytecode.bin")];
    scenario_require_bytes(r, "portable identity", x.data, x.size, y.data, y.size, false);
}

static void
merge(suite_t *s, const char *out, const char *const *inputs, size_t count, size_t variants) {
    scenario_run_t *r = s->r;
    const char **args = language_owned(r, CALLOC((count + 2) * sizeof(*args)));
    args[0] = out;
    for (size_t i = 0; i < count; i++)
        args[i + 1] = inputs[i];
    scenario_command_result_t v =
        invoke(s, s->c->merge, args, producer_env(s, s->c->cc), false, NULL);
    REQUIRE(v.err_size == 0);
    const char *suffix =
        fmt(r, ": \"delivery\"@\"opaque-release\"; %zu native variants\n", variants);
    char *text = language_owned(r, ALLOC(v.out_size + 1));
    size_t n = 0;
    for (size_t i = 0; i < v.out_size; i++) {
        char c = ((const char *)v.out)[i];
        if (c == '\r' && i + 1 < v.out_size && ((const char *)v.out)[i + 1] == '\n')
            continue;
        text[n++] = c;
    }
    text[n] = 0;
    REQUIRE(n >= strlen(suffix) && text[0] == '"' && !strcmp(text + n - strlen(suffix), suffix));
    contents(s, out, true);
}

static const char *expected(suite_t *s) {
    return read_bytes(s->r, language_join(s->r, s->data, "expected.txt")).data;
}

static void execute(suite_t *s, const char *path, const char *mode, bool fail) {
    scenario_run_t *r = s->r;
    const char *report = language_join(r, s->root, fmt(r, "counters-%u.txt", s->count));
    invoke(s,
           s->c->goat,
           ARGS("run",
                path,
                "--native",
                mode,
                "--save-native",
                report,
                "--cc",
                language_join(r, s->empty, "no-cc")),
           s->consumer,
           fail,
           fail ? "" : expected(s));
    if (!fail) {
        bytes_t b = read_bytes(r, report);
        const char *p = b.data;
        size_t succeeded = 0, bound = 0;
        bool saw_succeeded = false, saw_bound = false;
        while (*p) {
            const char *end = strchr(p, '\n');
            if (!end)
                end = p + strlen(p);
            if (!strncmp(p, "succeeded=", 10)) {
                char *tail;
                unsigned long value = strtoul(p + 10, &tail, 10);
                REQUIRE(tail == end || (*tail == '\r' && tail + 1 == end));
                succeeded = value;
                REQUIRE(!saw_succeeded);
                saw_succeeded = true;
            }
            if (!strncmp(p, "bound=", 6)) {
                char *tail;
                unsigned long value = strtoul(p + 6, &tail, 10);
                REQUIRE(tail == end || (*tail == '\r' && tail + 1 == end));
                bound = value;
                REQUIRE(!saw_bound);
                saw_bound = true;
            }
            p = *end ? end + 1 : end;
        }
        REQUIRE(saw_succeeded && saw_bound);
        if (!strcmp(mode, "off"))
            REQUIRE(succeeded == 0);
        else
            REQUIRE(succeeded > 0 && bound > 0);
    }
}

static yyjson_mut_doc *inspect(suite_t *s, const char *path, bool fail, bool russian) {
    scenario_run_t *r = s->r;
    const char *locale = russian ? "ru_RU.UTF-8" : "en_US.UTF-8";
    const char *const *env = env_set(r, env_set(r, s->consumer, "LANG", locale), "LC_ALL", locale);
    scenario_command_result_t v = invoke(s, s->c->inspect, ARGS("--json", path), env, fail, NULL);
    REQUIRE(v.err_size == 0);
    yyjson_mut_doc *report = parse(r, (bytes_t){v.out, v.out_size});
    jval_t *root = yyjson_mut_doc_get_root(report),
           *schema = yyjson_mut_doc_get_root(s->report_schema);
    char error[512];
    scenario_require(
        r,
        inspect_report_schema(root, schema, schema, s->c->format, error, sizeof(error)),
        error);
    REQUIRE(yyjson_mut_get_uint(get(root, "reportVersion")) == 1
            && yyjson_mut_is_bool(get(root, "success"))
            && yyjson_mut_get_bool(get(root, "success")) == !fail);
    size_t errors = 0;
    jval_t *diagnostics = get(root, "diagnostics");
    for (size_t i = 0; i < yyjson_mut_arr_size(diagnostics); i++)
        if (eq(get(yyjson_mut_arr_get(diagnostics, i), "severity"), "error"))
            errors++;
    REQUIRE((errors > 0) == fail);
    return report;
}

static jval_t *variants(suite_t *s, yyjson_mut_doc *report) {
    scenario_run_t *r = s->r;
    jval_t *modules = get(yyjson_mut_doc_get_root(report), "modules"), *found = NULL;
    size_t n = 0;
    for (size_t i = 0; i < yyjson_mut_arr_size(modules); i++) {
        jval_t *module = yyjson_mut_arr_get(modules, i);
        if (eq(get(get(module, "metadata"), "name"), "delivery")) {
            found = get(module, "native");
            n++;
        }
    }
    REQUIRE(n == 1 && yyjson_mut_is_arr(found));
    return found;
}

static void error_codes_equal(suite_t *s, yyjson_mut_doc *a, yyjson_mut_doc *b) {
    scenario_run_t *r = s->r;
    jval_t *x = get(yyjson_mut_doc_get_root(a), "diagnostics"),
           *y = get(yyjson_mut_doc_get_root(b), "diagnostics");
    for (unsigned pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < yyjson_mut_arr_size(x); i++) {
            jval_t *v = yyjson_mut_arr_get(x, i);
            if (!eq(get(v, "severity"), "error"))
                continue;
            bool found = false;
            for (size_t j = 0; j < yyjson_mut_arr_size(y); j++) {
                jval_t *w = yyjson_mut_arr_get(y, j);
                if (eq(get(w, "severity"), "error")
                    && format_json_equal(get(v, "code"), get(w, "code")))
                    found = true;
            }
            REQUIRE(found);
        }
        jval_t *swap = x;
        x = y;
        y = swap;
    }
}

static void localized_failure(suite_t *s, const char *path) {
    yyjson_mut_doc *a = inspect(s, path, true, false), *b = inspect(s, path, true, true);
    error_codes_equal(s, a, b);
}

static void
write_archive(suite_t *s, const char *path, archive_t *a, const char *change, const char *method) {
    scenario_run_t *r = s->r;
    format_zip_entry_t *parts = language_owned(r, CALLOC(a->count * sizeof(*parts)));
    for (size_t i = 0; i < a->count; i++) {
        const char *name = fmt(r, "%.*s", (int)a->entries[i].name_size, a->entries[i].name);
        bytes_t bytes = a->parts[i];
        if (change && !strcmp(change, "json") && !strcmp(name, "module.json"))
            bytes = (bytes_t){"{bad json", 9};
        if (change
            && ((!strcmp(change, "native-hash")
                 && !strcmp(name,
                            str(r, get(yyjson_mut_arr_get(get(doc(a), "native"), 0), "library"))))
                || (!strcmp(change, "bytecode-hash") && !strcmp(name, "bytecode.bin")))) {
            const char *append = !strcmp(change, "native-hash") ? "changed native payload"
                                                                : "changed portable payload";
            size_t n = bytes.size + strlen(append);
            char *data = language_owned(r, ALLOC(n));
            memcpy(data, bytes.data, bytes.size);
            memcpy(data + bytes.size, append, strlen(append));
            bytes = (bytes_t){data, n};
        }
        if (change && !strcmp(change, "unknown-abi") && !strcmp(name, "module.json")) {
            yyjson_mut_doc *copy = yyjson_mut_doc_new(NULL);
            if (copy)
                scenario_defer(r, free_doc, copy);
            REQUIRE(copy);
            jval_t *root = yyjson_mut_val_mut_copy(copy, doc(a));
            REQUIRE(root);
            yyjson_mut_doc_set_root(copy, root);
            jval_t *native = yyjson_mut_arr_get(get(root, "native"), 0);
            REQUIRE(yyjson_mut_obj_put(native,
                                       yyjson_mut_str(copy, "goatAbiVersion"),
                                       yyjson_mut_uint(copy, 2)));
            size_t n;
            char *json = format_json_canonical(root, &n);
            if (json)
                scenario_defer(r, free_plain, json);
            REQUIRE(json);
            bytes = (bytes_t){json, n};
        }
        bool deflated = strcmp(method, "stored") && (strcmp(method, "mixed") || i % 2 == 0);
        format_zip_entry_init(parts + i, name, bytes.data, bytes.size, deflated);
    }
    size_t n;
    char error[512];
    unsigned char *raw =
        format_zip_build(parts, a->count, NULL, 0, false, &n, error, sizeof(error));
    if (raw)
        scenario_defer(r, free_plain, raw);
    REQUIRE(raw);
    write_bytes(r, path, (bytes_t){raw, n});
}

static unsigned le16(const unsigned char *p) {
    return p[0] | ((unsigned)p[1] << 8);
}
#ifdef _WIN32
static unsigned le32(const unsigned char *p) {
    return le16(p) | ((unsigned)le16(p + 2) << 16);
}
#endif
static unsigned architecture(suite_t *s, jval_t *target) {
    scenario_run_t *r = s->r;
    bytes_t image = read_bytes(r, s->c->goat);
    const unsigned char *p = image.data;
    const char *arch = NULL;
    unsigned bits = 0;
#ifdef _WIN32
    REQUIRE(image.size >= 64);
    unsigned pe = le32(p + 0x3c);
    REQUIRE(pe <= image.size && image.size - pe >= 6 && !memcmp(p + pe, "PE\0\0", 4));
    unsigned machine = le16(p + pe + 4);
    if (machine == 0x14c) {
        arch = "x86";
        bits = 32;
    } else if (machine == 0x8664) {
        arch = "x86_64";
        bits = 64;
    }
#else
    REQUIRE(image.size >= 20 && !memcmp(p, "\177ELF", 4) && p[5] == 1 && p[4] == 2);
    unsigned machine = le16(p + 18);
    if (machine == 62)
        arch = "x86_64";
    else if (machine == 183)
        arch = "aarch64";
    bits = 64;
#endif
    REQUIRE(arch && eq(get(target, "architecture"), arch)
            && yyjson_mut_get_uint(get(target, "bits")) == bits);
    return bits;
}

static bool selected(config_t *c, const char *id) {
    return !strcmp(c->id, "end-to-end") || !strcmp(c->id, id);
}

/* Snapshot names and bytes, not just timestamps, to detect any consumer mutation. */
typedef struct snapshot_t {
    const char *path;
    bytes_t bytes;
    struct snapshot_t *next;
} snapshot_t;

static void snapshot(scenario_run_t *r, const char *path, snapshot_t **head) {
    fs_entries_t *list = entries(r, path);
    for (size_t i = 0; i < list->count; i++) {
        const char *p = language_join(r, path, list->names[i]);
        fs_info_t info;
        fs_error_t e = {0};
        bool ok = inspect_fs_path(p, false, &info, &e);
        clear_fs_error(&e);
        REQUIRE(ok);
        if (info.kind == FS_DIRECTORY)
            snapshot(r, p, head);
        else {
            REQUIRE(info.kind == FS_FILE);
            size_t n = strlen(p);
            if (n < 5 || strcmp(p + n - 5, ".gbin"))
                continue;
            snapshot_t *entry = language_owned(r, CALLOC(sizeof(*entry)));
            *entry = (snapshot_t){p, read_bytes(r, p), *head};
            *head = entry;
        }
    }
}

static void unchanged(scenario_run_t *r, const char *path, snapshot_t *before) {
    snapshot_t *after = NULL;
    snapshot(r, path, &after);
    size_t a = 0, b = 0;
    for (snapshot_t *p = before; p; p = p->next) {
        b++;
        bool found = false;
        for (snapshot_t *q = after; q; q = q->next)
            if (!strcmp(p->path, q->path)) {
                found = true;
                scenario_require_bytes(r,
                                       "unchanged delivery",
                                       p->bytes.data,
                                       p->bytes.size,
                                       q->bytes.data,
                                       q->bytes.size,
                                       false);
            }
        REQUIRE(found);
    }
    for (snapshot_t *p = after; p; p = p->next)
        a++;
    REQUIRE(a == b);
}

static void controls(scenario_run_t *r, void *unused) {
    config_t *config = unused;
    const char *compiler = language_find_executable(r, config->cc);
    REQUIRE(language_exists(r, compiler));
    const char *missing = "goat-delivery-compiler-does-not-exist-388";
    REQUIRE(!strcmp(language_find_executable(r, missing), missing));
    const char *missing_path =
        language_join(r, scenario_work_directory(scenario_run_context(r)), "missing compiler");
    REQUIRE(!strcmp(language_find_executable(r, missing_path), missing_path));
    const char *data =
        language_join(r, scenario_data_directory(scenario_run_context(r)), "module-delivery");
    yyjson_mut_doc *manifest = parse(r, read_bytes(r, language_join(r, data, "manifest.json")));
    jval_t *files = get(yyjson_mut_doc_get_root(manifest), "files");
    REQUIRE(yyjson_mut_is_obj(files) && yyjson_mut_obj_size(files) > 0);
    size_t index, max;
    jval_t *key, *value;
    yyjson_mut_obj_foreach(files, index, max, key, value) {
        bytes_t b = read_bytes(r, language_join(r, data, str(r, key)));
        char hash[65];
        format_binary_sha256(b.data, b.size, hash);
        REQUIRE(eq(value, hash));
        char *mutated = language_owned(r, ALLOC(b.size + 1));
        memcpy(mutated, b.data, b.size);
        mutated[b.size] = '!';
        format_binary_sha256(mutated, b.size + 1, hash);
        REQUIRE(!eq(value, hash));
    }
    bytes_t stdout_golden = read_bytes(r, language_join(r, data, "expected.txt"));
    REQUIRE(stdout_golden.size > 0
            && ((const char *)stdout_golden.data)[stdout_golden.size - 1] == '\n');
    /* ZIP oracle itself must reject a damaged payload, independent of Goat. */
    format_zip_entry_t entry;
    format_zip_entry_init(&entry, "bytecode.bin", "abc", 3, false);
    char error[512];
    size_t size;
    unsigned char *zip = format_zip_build(&entry, 1, NULL, 0, false, &size, error, sizeof(error));
    if (zip)
        scenario_defer(r, free_plain, zip);
    REQUIRE(zip);
    format_zip_info_t info;
    REQUIRE(format_zip_inspect(zip, size, 0, &info, error, sizeof(error)));
    size_t count;
    unsigned char *bytes = format_zip_extract(zip, size, 0, 1024, &count, error, sizeof(error));
    if (bytes)
        scenario_defer(r, free_plain, bytes);
    REQUIRE(bytes && count == 3);
    zip[info.data_offset] ^= 1;
    bytes = format_zip_extract(zip, size, 0, 1024, &count, error, sizeof(error));
    if (bytes)
        scenario_defer(r, free_plain, bytes);
    REQUIRE(!bytes);
    printf("Module delivery frozen-input and corruption controls passed\n");
}

static void body(scenario_run_t *r, void *user) {
    config_t config = *(config_t *)user;
    config_t *c = &config;
    c->cc = language_find_executable(r, c->cc);
    suite_t s = {.r = r, .c = c};
    s.root = language_join(r, scenario_work_directory(scenario_run_context(r)), "delivery work");
    directory(r, s.root);
    s.data = language_join(r, scenario_data_directory(scenario_run_context(r)), "module-delivery");
    s.schema = parse(r, read_bytes(r, language_join(r, c->format, "schema-source.json")));
    s.report_schema =
        parse(r, read_bytes(r, language_join(r, c->format, "inspect-report.schema.json")));
    s.source = language_join(r, s.root, "source");
    s.modules = language_join(r, s.root, "staging");
    s.delivered = language_join(r, s.root, "delivered");
    directory(r, s.delivered);
    build(&s, "leaf", "2", "leaf2.goat", "required", s.modules, c->cc, "host");
    build(&s, "leaf", "1", "leaf1.goat", "required", s.modules, c->cc, "host");
    build(&s, "middle", "release", "middle.goat", "required", s.modules, c->cc, "host");
    const char *app = build(&s,
                            "delivery",
                            "opaque-release",
                            "delivery.goat",
                            "required",
                            s.modules,
                            c->cc,
                            "host");
    const char *off = build(&s,
                            "delivery",
                            "opaque-release",
                            "delivery.goat",
                            "off",
                            language_join(r, s.root, "off"),
                            c->cc,
                            "host");
    const char *automatic = build(&s,
                                  "delivery",
                                  "opaque-release",
                                  "delivery.goat",
                                  "auto",
                                  language_join(r, s.root, "auto"),
                                  c->cc,
                                  "host");
    archive_t *native = contents(&s, app, false), *plain_archive = contents(&s, off, false),
              *auto_archive = contents(&s, automatic, false);
    compare_identity(&s, native, plain_archive);
    compare_identity(&s, native, auto_archive);
    REQUIRE(yyjson_mut_arr_size(get(doc(native), "native")) > 0
            && yyjson_mut_arr_size(get(doc(auto_archive), "native")) > 0
            && yyjson_mut_arr_size(get(doc(plain_archive), "native")) == 0);
    yyjson_mut_doc *manifest = parse(r, read_bytes(r, language_join(r, s.data, "manifest.json")));
    jval_t *golden = yyjson_mut_doc_get_root(manifest);
    REQUIRE(format_json_equal(get(doc(native), "mainHash"), get(golden, "mainHash")));
    char hash[65];
    bytes_t code = native->parts[member(r, native, "bytecode.bin")];
    format_binary_sha256(code.data, code.size, hash);
    REQUIRE(eq(get(golden, "bytecodeSha256"), hash));
    jval_t *target = get(yyjson_mut_arr_get(get(doc(native), "native"), 0), "target");
    unsigned bits = architecture(&s, target);
    copy_tree(r, s.modules, language_join(r, s.delivered, "modules"));
    const char *portable = language_join(r, s.delivered, "app.gbin"),
               *plain = language_join(r, s.delivered, "plain.gbin");
    write_bytes(r, portable, read_bytes(r, app));
    write_bytes(r, plain, read_bytes(r, off));
    const char *replacement = build(&s,
                                    "leaf",
                                    "2",
                                    "alternate.goat",
                                    "required",
                                    language_join(r, s.root, "alternate"),
                                    c->cc,
                                    "host");
    bytes_t alternate = read_bytes(r, replacement);
    const char *merged = language_join(r, s.delivered, "merged-roundtrip.gbin");
    merge(&s, merged, ARGS(app, off), 2, 1);
    const char *mixed = NULL, *foreign_only = NULL;
#ifdef _WIN32
    const char *foreign[2] = {0};
    size_t foreign_count = 0;
    const char *environments[] = {"MINGW64", "UCRT64"}, *families[] = {"msvcrt", "ucrt"},
               *targets[] = {"windows-x86_64-mingw64", "windows-x86_64-ucrt64"};
    for (size_t i = 0; i < 2; i++) {
        fs_error_t e = {0};
        char *compiler = fs_environment(fmt(r, "GOAT_DELIVERY_%s_CC", environments[i]), &e);
        clear_fs_error(&e);
        language_owned(r, compiler);
        /* canonical_fs_path requires an existing absolute Windows path here. */
        REQUIRE((strlen(compiler) >= 3 && compiler[1] == ':'
                 && (compiler[2] == '/' || compiler[2] == '\\'))
                || (compiler[0] == '/' && compiler[1] == '/'));
        REQUIRE(language_exists(r, compiler));
        scenario_command_result_t machine =
            invoke(&s, compiler, ARGS("-dumpmachine"), producer_env(&s, compiler), false, NULL);
        const char *machine_text = machine.out;
        size_t n = machine.out_size;
        while (n
               && (machine_text[n - 1] == '\n' || machine_text[n - 1] == '\r'
                   || machine_text[n - 1] == ' ' || machine_text[n - 1] == '\t'))
            n--;
        scenario_require_bytes(r,
                               "foreign compiler machine",
                               "x86_64-w64-mingw32",
                               sizeof("x86_64-w64-mingw32") - 1,
                               machine_text,
                               n,
                               false);
        scenario_command_result_t macros = invoke(&s,
                                                  compiler,
                                                  ARGS("-dM", "-E", "-x", "c", "-"),
                                                  producer_env(&s, compiler),
                                                  false,
                                                  NULL);
        REQUIRE(strstr(macros.out, "#define __x86_64__ 1"));
        const char *output = language_join(r, s.root, fmt(r, "producer-%s", environments[i]));
        const char *built = build(&s,
                                  "delivery",
                                  "opaque-release",
                                  "delivery.goat",
                                  "required",
                                  output,
                                  compiler,
                                  targets[i]);
        archive_t *a = contents(&s, built, false);
        REQUIRE(format_json_equal(get(doc(a), "mainHash"), get(doc(native), "mainHash")));
        jval_t *foreign_target = get(yyjson_mut_arr_get(get(doc(a), "native"), 0), "target");
        REQUIRE(eq(get(get(foreign_target, "runtime"), "family"), families[i])
                && eq(get(foreign_target, "architecture"), "x86_64"));
        if (bits == 32 || !eq(get(get(target, "runtime"), "family"), families[i]))
            foreign[foreign_count++] = built;
    }
    mixed = language_join(r, s.delivered, "mixed-windows.gbin");
    foreign_only = language_join(r, s.delivered, "foreign-windows.gbin");
    const char *inputs[3] = {app, foreign[0], foreign[1]};
    merge(&s, mixed, inputs, foreign_count + 1, bits == 32 ? 3 : 2);
    merge(&s, foreign_only, foreign, foreign_count, bits == 32 ? 2 : 1);
    archive_t *mixed_archive = contents(&s, mixed, true);
    jval_t *natives = get(doc(mixed_archive), "native");
    REQUIRE(yyjson_mut_arr_size(natives) == (bits == 32 ? 3 : 2));
    bool saw_msvcrt = false, saw_ucrt = false;
    for (size_t i = 0; i < yyjson_mut_arr_size(natives); i++) {
        jval_t *family =
            get(get(get(yyjson_mut_arr_get(natives, i), "target"), "runtime"), "family");
        REQUIRE(eq(family, "msvcrt") || eq(family, "ucrt"));
        saw_msvcrt |= eq(family, "msvcrt");
        saw_ucrt |= eq(family, "ucrt");
    }
    REQUIRE(saw_msvcrt && saw_ucrt);
#else
    (void)bits;
#endif
    remove_tree(r, s.source);
    remove_tree(r, s.modules);
    remove_tree(r, language_join(r, s.root, "off"));
    remove_tree(r, language_join(r, s.root, "auto"));
    remove_tree(r, language_join(r, s.root, "alternate"));
#ifdef _WIN32
    remove_tree(r, language_join(r, s.root, "producer-MINGW64"));
    remove_tree(r, language_join(r, s.root, "producer-UCRT64"));
#endif
    no_sources(r, s.root);
    s.empty = language_join(r, s.root, "no-tools");
    directory(r, s.empty);
    s.consumer = env_set(r, NULL, "PATH", s.empty);
    s.consumer = env_set(r, s.consumer, "CC", language_join(r, s.empty, "no-cc"));
    s.consumer = env_set(r, s.consumer, "CXX", language_join(r, s.empty, "no-cxx"));
    s.consumer = env_set(r, s.consumer, "GOAT_MODULE_PATH", "");
    s.consumer = env_set(r, s.consumer, "MSYSTEM", "deliberately-not-the-build-runtime");
    const char *modes[] = {"off", "auto", "required"};
    if (selected(c, "execution")) {
        snapshot_t *before = NULL;
        snapshot(r, s.delivered, &before);
        for (size_t i = 0; i < 3; i++)
            execute(&s, portable, modes[i], false);
        execute(&s, merged, "required", false);
        execute(&s, plain, "off", false);
        execute(&s, plain, "required", true);
        unchanged(r, s.delivered, before);
        inspect(&s, portable, false, false);
    }
    if (selected(c, "unknown-abi")) {
        const char *unknown = language_join(r, s.delivered, "unknown-abi.gbin");
        write_archive(&s, unknown, native, "unknown-abi", "deflated");
        archive_t *a = contents(&s, unknown, false);
        REQUIRE(format_json_equal(get(doc(a), "mainHash"), get(doc(native), "mainHash")));
        execute(&s, unknown, "off", false);
        execute(&s, unknown, "auto", false);
        execute(&s, unknown, "required", true);
        jval_t *own = variants(&s, inspect(&s, unknown, false, false));
        REQUIRE(yyjson_mut_arr_size(own) > 0);
        for (size_t i = 0; i < yyjson_mut_arr_size(own); i++) {
            jval_t *v = yyjson_mut_arr_get(own, i);
            REQUIRE(eq(get(get(v, "compatibility"), "state"), "incompatible")
                    && eq(get(get(v, "loading"), "state"), "not_checked")
                    && eq(get(get(get(v, "loading"), "reason"), "code"), "NO_COMPATIBLE_VARIANT"));
        }
    }
    if (mixed && selected(c, "windows-runtimes")) {
        const char *fake[] = {"MINGW32", "MINGW64", "UCRT64"};
        for (size_t i = 0; i < 3; i++) {
            s.consumer = env_set(r, s.consumer, "MSYSTEM", fake[i]);
            for (size_t j = 0; j < 3; j++)
                execute(&s, mixed, modes[j], false);
        }
        s.consumer = env_set(r, s.consumer, "MSYSTEM", "deliberately-not-the-build-runtime");
        jval_t *own = variants(&s, inspect(&s, mixed, false, false)), *compatible = NULL;
        size_t count = 0;
        for (size_t i = 0; i < yyjson_mut_arr_size(own); i++) {
            jval_t *v = yyjson_mut_arr_get(own, i);
            if (eq(get(get(v, "compatibility"), "state"), "compatible")) {
                compatible = v;
                count++;
                REQUIRE(eq(get(get(v, "loading"), "state"), "checked")
                        && eq(get(get(get(v, "loading"), "probe"), "state"), "passed"));
            } else
                REQUIRE(eq(get(get(v, "loading"), "state"), "not_checked"));
        }
        REQUIRE(count == 1);
        archive_t *a = contents(&s, mixed, false);
        jval_t *natives = get(doc(a), "native");
        bool found = false;
        for (size_t i = 0; i < yyjson_mut_arr_size(natives); i++) {
            jval_t *v = yyjson_mut_arr_get(natives, i);
            if (format_json_equal(get(v, "library"), get(compatible, "library"))) {
                found = true;
                jval_t *t = get(v, "target");
                REQUIRE(format_json_equal(get(t, "architecture"), get(target, "architecture"))
                        && format_json_equal(get(get(t, "runtime"), "family"),
                                             get(get(target, "runtime"), "family")));
            }
        }
        REQUIRE(found);
        execute(&s, foreign_only, "off", false);
        execute(&s, foreign_only, "auto", false);
        execute(&s, foreign_only, "required", true);
        own = variants(&s, inspect(&s, foreign_only, false, false));
        REQUIRE(yyjson_mut_arr_size(own) > 0);
        for (size_t i = 0; i < yyjson_mut_arr_size(own); i++) {
            jval_t *v = yyjson_mut_arr_get(own, i);
            REQUIRE(eq(get(get(v, "compatibility"), "state"), "incompatible")
                    && eq(get(get(v, "loading"), "state"), "not_checked"));
        }
    }
    if (selected(c, "dependencies")) {
        inspect(&s, language_join(r, s.delivered, "absent.gbin"), true, false);
        const char *dependency = language_join(r, s.delivered, "modules/leaf/2.gbin");
        bytes_t original = read_bytes(r, dependency);
        language_remove(r, dependency);
        for (size_t i = 0; i < 3; i++)
            execute(&s, portable, modes[i], true);
        localized_failure(&s, portable);
        write_bytes(r, dependency, alternate);
        for (size_t i = 0; i < 3; i++)
            execute(&s, portable, modes[i], true);
        inspect(&s, portable, true, false);
        write_bytes(r, dependency, original);
        execute(&s, portable, "required", false);
    }
    const char *bad[] = {"native-hash", "bytecode-hash", "json"};
    for (size_t i = 0; i < 3; i++)
        if (selected(c, fmt(r, "integrity-%s", bad[i]))) {
            const char *path = language_join(r, s.delivered, fmt(r, "%s.gbin", bad[i]));
            write_archive(&s, path, native, bad[i], "deflated");
            for (size_t j = 0; j < 3; j++)
                execute(&s, path, modes[j], true);
            localized_failure(&s, path);
        }
    const char *methods[] = {"stored", "mixed"};
    for (size_t i = 0; i < 2; i++)
        if (selected(c, fmt(r, "compression-%s", methods[i]))) {
            const char *path = language_join(r, s.delivered, fmt(r, "%s.gbin", methods[i]));
            write_archive(&s, path, native, NULL, methods[i]);
            archive_t *a = contents(&s, path, false);
            REQUIRE(format_json_equal(get(doc(a), "mainHash"), get(doc(native), "mainHash"))
                    && a->count == native->count);
            for (size_t j = 0; j < native->count; j++) {
                const char *name =
                    fmt(r, "%.*s", (int)native->entries[j].name_size, native->entries[j].name);
                bytes_t b = a->parts[member(r, a, name)];
                scenario_require_bytes(r,
                                       "repacked entry",
                                       native->parts[j].data,
                                       native->parts[j].size,
                                       b.data,
                                       b.size,
                                       false);
            }
            execute(&s, path, "required", false);
            inspect(&s, path, false, false);
        }
    printf("Module delivery end-to-end checks passed: %u\n", s.count);
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
    bool keep = false, timeout_set = false, valid_options = argc >= 9;
    unsigned timeout_seconds = 600;
    for (int i = 9; valid_options && i < argc; i++) {
        if (!strcmp(argv[i], "--keep-work") && !keep)
            keep = true;
        else if (!strcmp(argv[i], "--timeout-seconds") && !timeout_set && i + 1 < argc) {
            timeout_set = true;
            const char *number = argv[++i];
            unsigned value = 0;
            valid_options = *number != '\0';
            for (; valid_options && *number; number++) {
                if (*number < '0' || *number > '9' || value > 600 / 10)
                    valid_options = false;
                else {
                    value = value * 10 + (unsigned)(*number - '0');
                    valid_options = value <= 600;
                }
            }
            valid_options = valid_options && value > 30;
            timeout_seconds = value;
        } else
            valid_options = false;
    }
    if (valid_options) {
        char *paths[9] = {0};
        bool valid = true;
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
        if (valid) {
            static const char *names[] = {"producer",
                                          "execution",
                                          "unknown-abi",
                                          "windows-runtimes",
                                          "dependencies",
                                          "integrity-native-hash",
                                          "integrity-bytecode-hash",
                                          "integrity-json",
                                          "compression-stored",
                                          "compression-mixed",
                                          "end-to-end",
                                          "controls"};

            enum { COUNT = sizeof(names) / sizeof(names[0]) };

            config_t configs[COUNT];
            scenario_definition_t definitions[COUNT];
            char ids[COUNT][128];
            for (size_t i = 0; i < COUNT; i++) {
                snprintf(ids[i], sizeof(ids[i]), "integration.module_delivery.%s", names[i]);
                configs[i] = (config_t){paths[2], paths[3], paths[4], paths[5], paths[8], names[i]};
                definitions[i] = (scenario_definition_t){ids[i],
                                                         (timeout_seconds - 30) * 1000,
                                                         body,
                                                         configs + i,
                                                         NULL};
                if (!strcmp(names[i], "controls"))
                    definitions[i].body = controls;
#ifndef _WIN32
                if (!strcmp(names[i], "windows-runtimes"))
                    definitions[i].skip_reason =
                        "Windows PE/MSVCRT/UCRT compiler delivery applies only on Windows";
#endif
            }
            scenario_options_t options = {.build_directory = paths[6], .data_directory = paths[7]};
            const char *selection[] = {argv[1], keep ? "--keep-work" : NULL};
            result = scenario_registry_main(definitions, COUNT, keep ? 2 : 1, selection, &options);
        }
        for (int i = 2; i < 9; i++)
            FREE(paths[i]);
    } else
        fputs("usage: module_delivery_scenarios ID GOAT INSPECT MERGE CC BUILD DATA FORMAT_ROOT "
              "[--keep-work] [--timeout-seconds 31..600]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
