/** @file module_musl_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real glibc/musl delivery acceptance (#390).
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
    const char *goat, *inspect, *merge, *selector, *cc, *family, *format, *id, *destination;
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
    bool input;
} command_t;

static void command_body(scenario_run_t *r, void *p) {
    command_t *c = p;
    child_options_t options = {.executable = c->exe,
                               .working_directory = c->cwd,
                               .arguments = c->args,
                               .environment = c->env,
                               .empty_stdin = !c->input,
                               .stdin_data = c->input ? "7\n" : NULL,
                               .stdin_size = c->input ? 2 : 0};
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
    command_t command = {exe,
                         s->root,
                         arguments,
                         env,
                         {0},
                         expected != NULL && !strcmp(exe, s->c->goat) && strcmp(args[0], "build")};
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

static int version_compare(const uint32_t a[4], const uint32_t b[4]) {
    for (size_t i = 0; i < 4; i++)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
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

static void identical(scenario_run_t *r, bytes_t a, bytes_t b) {
    scenario_require_bytes(r, "immutable portable delivery", a.data, a.size, b.data, b.size, false);
}

static const char *architecture(scenario_run_t *r) {
    (void)r;
#if defined(__x86_64__)
    return "x86_64";
#elif defined(__aarch64__)
    return "aarch64";
#else
    REQUIRE(false);
    return NULL;
#endif
}

static uint64_t integer(scenario_run_t *r, jval_t *v) {
    uint64_t n = 0;
    REQUIRE(unsigned_integer(v, &n));
    return n;
}

static archive_t *checked(suite_t *s, const char *path, const char *family) {
    scenario_run_t *r = s->r;
    archive_t *a = contents(s, path, false);
    jval_t *t = target(r, a), *n = yyjson_mut_arr_get(get(doc(a), "native"), 0);
    REQUIRE(integer(r, get(n, "goatAbiVersion")) == 1);
    REQUIRE(eq(get(t, "os"), "linux") && eq(get(t, "architecture"), architecture(r)));
    REQUIRE(integer(r, get(t, "bits")) == 64 && eq(get(get(t, "runtime"), "family"), family));
    jval_t *f = get(t, "cpuFeatures");
    bool x = !strcmp(architecture(r), "x86_64");
    REQUIRE(yyjson_mut_is_arr(f) && yyjson_mut_arr_size(f) == (x ? 1 : 2));
    REQUIRE(eq(yyjson_mut_arr_get(f, 0), x ? "sse2" : "fp"));
    if (!x)
        REQUIRE(eq(yyjson_mut_arr_get(f, 1), "neon"));
    uint32_t v[4];
    version(r, get(get(t, "runtime"), "minimumVersion"), v);
    REQUIRE(v[0] || v[1] || v[2] || v[3]);
    return a;
}

static scenario_command_result_t host(suite_t *s) {
    scenario_run_t *r = s->r;
    scenario_command_result_t h = invoke(s, s->c->selector, ARGS("host"), NULL, false, NULL);
    jval_t *v = yyjson_mut_doc_get_root(parse(r, (bytes_t){h.out, h.out_size}));
    REQUIRE(integer(r, get(v, "runtime")) == (!strcmp(s->c->family, "glibc") ? 1 : 2));
    REQUIRE(yyjson_mut_is_true(get(get(v, "runtimeVersion"), "known")));
    return h;
}

static void print_version(scenario_run_t *r, jval_t *v) {
    putchar('[');
    for (size_t i = 0; i < yyjson_mut_arr_size(v); i++)
        printf("%s%llu", i ? ", " : "", (unsigned long long)integer(r, yyjson_mut_arr_get(v, i)));
    putchar(']');
}

static void produce(suite_t *s) {
    scenario_run_t *r = s->r;
    config_t *c = s->c;
    scenario_command_result_t h = host(s);
    const char *source = language_join(r, s->root, "main.goat"),
               *descriptor = language_join(r, s->root, "module.gmod");
    write_bytes(r, source, read_bytes(r, language_join(r, s->data, "main.goat")));
    write_bytes(r, descriptor, read_bytes(r, language_join(r, s->data, "module.gmod")));
    const char *t =
        fmt(r, "linux-%s-%s", architecture(r), !strcmp(c->family, "musl") ? "musl" : "gnu");
    scenario_command_result_t listed =
        invoke(s, c->goat, ARGS("--list-native-targets", "--cc", c->cc), NULL, false, NULL);
    bool found = false;
    for (size_t i = 0; i + strlen(t) <= listed.out_size; i++)
        found |= !memcmp(listed.out + i, t, strlen(t));
    REQUIRE(found);
    invoke(s,
           c->goat,
           ARGS("build", descriptor, "--native", "required", "--cc", c->cc, "--native-target", t),
           NULL,
           false,
           NULL);
    archive_t *a = checked(s, language_join(r, s->root, "modules/libc-delivery/1.gbin"), c->family);
    uint32_t minimum[4], actual[4];
    version(r, get(get(target(r, a), "runtime"), "minimumVersion"), minimum);
    jval_t *hv = yyjson_mut_doc_get_root(parse(r, (bytes_t){h.out, h.out_size}));
    version(r, get(get(hv, "runtimeVersion"), "components"), actual);
    REQUIRE(version_compare(minimum, actual) <= 0);
    if (!strcmp(c->family, "musl"))
        REQUIRE(version_compare(minimum, actual) == 0);
    write_bytes(r, language_join(r, c->destination, fmt(r, "%s.gbin", c->family)), a->raw);
    write_bytes(r,
                language_join(r, c->destination, fmt(r, "%s-host.json", c->family)),
                (bytes_t){h.out, h.out_size});
    printf("Produced real %s %s minimum ", c->family, t);
    print_version(r, get(get(target(r, a), "runtime"), "minimumVersion"));
    putchar('\n');
}

static void verify(suite_t *s) {
    scenario_run_t *r = s->r;
    config_t *c = s->c;
    const char *families[] = {c->family, !strcmp(c->family, "musl") ? "glibc" : "musl"};
    archive_t *a[3];
    const char *paths[3];
    const char *names[] = {"own", "foreign", "mixed"};
    const char *original[2];
    for (size_t i = 0; i < 2; i++) {
        original[i] = language_join(r, c->destination, fmt(r, "%s.gbin", families[i]));
        a[i] = checked(s, original[i], families[i]);
        paths[i] = language_join(r, s->root, fmt(r, "%s.gbin", names[i]));
        write_bytes(r, paths[i], a[i]->raw);
    }
    REQUIRE(format_json_equal(get(doc(a[0]), "mainHash"), get(doc(a[1]), "mainHash")));
    identical(r,
              a[0]->parts[member(r, a[0], "bytecode.bin")],
              a[1]->parts[member(r, a[1], "bytecode.bin")]);
    scenario_command_result_t h = host(s);
    REQUIRE(format_json_equal(
        yyjson_mut_doc_get_root(parse(r, (bytes_t){h.out, h.out_size})),
        yyjson_mut_doc_get_root(parse(
            r,
            read_bytes(r, language_join(r, c->destination, fmt(r, "%s-host.json", c->family)))))));
    paths[2] = language_join(r, s->root, "mixed.gbin");
    invoke(s, c->merge, ARGS(paths[2], paths[0], paths[1]), NULL, false, NULL);
    a[2] = contents(s, paths[2], false);
    REQUIRE(format_json_equal(get(doc(a[0]), "mainHash"), get(doc(a[2]), "mainHash"))
            && yyjson_mut_arr_size(get(doc(a[2]), "native")) == 2);
    identical(r,
              a[0]->parts[member(r, a[0], "bytecode.bin")],
              a[2]->parts[member(r, a[2], "bytecode.bin")]);
    const char *absent = language_join(r, s->root, "absent");
    const char *const *env = env_set(r, NULL, "PATH", language_join(r, s->root, "no-compiler"));
    env = env_set(r, env, "CC", absent);
    env = env_set(r, env, "CXX", absent);
    env = env_set(r, env, "GOAT_MODULE_PATH", "");
    const char *modes[] = {"off", "auto", "required"};
    for (size_t i = 0; i < 3; i++) {
        scenario_command_result_t inspected =
            invoke(s, c->inspect, ARGS("--json", paths[i]), env, false, NULL);
        REQUIRE(yyjson_mut_is_true(
            get(yyjson_mut_doc_get_root(parse(r, (bytes_t){inspected.out, inspected.out_size})),
                "success")));
        for (size_t j = 0; j < 3; j++) {
            bool fail = i == 1 && j == 2;
            const char *report = language_join(r, s->root, fmt(r, "%s-%s.txt", names[i], modes[j]));
            scenario_command_result_t v = invoke(
                s,
                c->goat,
                ARGS(paths[i], "--native", modes[j], "--save-native", report, "--cc", absent),
                env,
                fail,
                fail ? "" : "49\n");
            if (fail) {
                REQUIRE(v.err_size);
                continue;
            }
            bytes_t b = read_bytes(r, report);
            bool native = i != 1 && j != 0;
            REQUIRE(counter(r, b, "succeeded") == (native ? 1 : 0));
            REQUIRE(native ? counter(r, b, "bound") > 0 : counter(r, b, "bound") == 0);
        }
    }
    for (size_t i = 0; i < 3; i++)
        identical(r, a[i]->raw, read_bytes(r, paths[i]));
    for (size_t i = 0; i < 2; i++)
        identical(r, a[i]->raw, read_bytes(r, original[i]));
    fs_entries_t *list = entries(r, s->root);
    for (size_t i = 0; i < list->count; i++) {
        size_t n = strlen(list->names[i]);
        REQUIRE(n < 5
                || (strcmp(list->names[i] + n - 5, ".goat")
                    && strcmp(list->names[i] + n - 5, ".gmod")));
    }
    printf("%s: own/mixed real native calls; foreign VM fallback/required rejection; all three "
           "modes; identical portable identity; immutable archives\n",
           c->family);
}

static void controls(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *values[] = {"0", "3", "-1", "\"0\"", "{}", "true", "null"};
    for (size_t i = 0; i < 7; i++) {
        uint64_t n = 0;
        jval_t *v = yyjson_mut_doc_get_root(parse(r, (bytes_t){values[i], strlen(values[i])}));
        REQUIRE(unsigned_integer(v, &n) == (i < 2));
        if (i < 2)
            REQUIRE(n == (i ? 3 : 0));
    }
    const char *versions[] = {"[0]", "[-1]", "[4294967296]", "[]", "[1,2,3,4,5]", "{}", "[1,2]"};
    for (size_t i = 0; i < 7; i++) {
        uint32_t out[4];
        jval_t *v = yyjson_mut_doc_get_root(parse(r, (bytes_t){versions[i], strlen(versions[i])}));
        REQUIRE(parse_version(v, out) == (i == 0 || i == 6));
        if (i == 0)
            REQUIRE(!out[0] && !out[1] && !out[2] && !out[3]);
        if (i == 6)
            REQUIRE(out[0] == 1 && out[1] == 2 && !out[2] && !out[3]);
    }
    uint32_t low[4] = {1, 2}, high[4] = {1, 3};
    REQUIRE(version_compare(low, high) < 0 && version_compare(high, low) > 0
            && !version_compare(low, low));
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
    size_t size;
    unsigned char *zip = format_zip_build(&entry, 1, NULL, 0, false, &size, error, sizeof(error));
    if (zip)
        scenario_defer(r, free_plain, zip);
    REQUIRE(zip);
    REQUIRE(format_zip_inspect(zip, size, 0, &info, error, sizeof(error)));
    zip[info.data_offset] ^= 1;
    size_t part_size;
    void *part = format_zip_extract(zip, size, 0, 1024, &part_size, error, sizeof(error));
    if (part)
        scenario_defer(r, free_plain, part);
    REQUIRE(!part);
    unsigned long n;
    const char *bad[] = {"succeeded=",
                         "succeeded=-1",
                         "succeeded=1x",
                         "succeeded=1\nsucceeded=2\n",
                         "bound=1",
                         "succeeded=999999999999999999999999999999999"};
    for (size_t i = 0; i < 6; i++)
        REQUIRE(!parse_counter((bytes_t){bad[i], strlen(bad[i])}, "succeeded", &n));
    REQUIRE(parse_counter((bytes_t){"succeeded=1\n", 12}, "succeeded", &n) && n == 1);
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
    suite_t s = {.r = r, .c = c};
    s.root = language_join(r, scenario_work_directory(scenario_run_context(r)), "libc work");
    directory(r, s.root);
    s.data = language_join(r, scenario_data_directory(scenario_run_context(r)), "module-musl");
    s.schema = parse(r, read_bytes(r, language_join(r, c->format, "schema-source.json")));
    if (!strcmp(c->id, "controls"))
        controls(&s);
    else {
        REQUIRE(c->destination);
        if (!strcmp(c->id, "produce")) {
            c->cc = language_find_executable(r, c->cc);
            produce(&s);
        } else
            verify(&s);
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
#if !defined(__linux__) || (!defined(__x86_64__) && !defined(__aarch64__))
    if (argc > 1 && strcmp(argv[1], "integration.module_musl.controls")) {
        fputs("Real libc delivery requires Linux x86_64 or AArch64\n", stderr);
#    ifdef _WIN32
        free_fs_arguments(argc, argv);
#    endif
        return 77;
    }
#endif
    int result = 1;
    bool keep = false, timeout_set = false, valid = argc >= 11;
    unsigned timeout = 900;
    const char *destination_arg = NULL;
    for (int i = 11; valid && i < argc; i++) {
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
    valid &= argc > 7 && (!strcmp(argv[7], "glibc") || !strcmp(argv[7], "musl"));
    if (valid) {
        char *paths[12] = {0};
        for (int i = 2; i < 11; i++) {
            fs_error_t e = {0};
            if (i == 6 || i == 7) {
                paths[i] = ALLOC(strlen(argv[i]) + 1);
                strcpy(paths[i], argv[i]);
            } else
                paths[i] = absolute_fs_path(argv[i], &e);
            clear_fs_error(&e);
            valid &= paths[i] != NULL;
        }
        if (destination_arg) {
            fs_error_t e = {0};
            paths[11] = absolute_fs_path(destination_arg, &e);
            clear_fs_error(&e);
            valid &= paths[11] != NULL;
        }
        if (valid) {
            const char *names[] = {"produce", "verify", "controls"};

            enum { COUNT = sizeof(names) / sizeof(names[0]) };

            config_t configs[COUNT];
            scenario_definition_t definitions[COUNT];
            char ids[COUNT][128];
            for (size_t i = 0; i < COUNT; i++) {
                snprintf(ids[i], sizeof(ids[i]), "integration.module_musl.%s", names[i]);
                configs[i] = (config_t){paths[2],
                                        paths[3],
                                        paths[4],
                                        paths[5],
                                        paths[6],
                                        paths[7],
                                        paths[10],
                                        names[i],
                                        paths[11]};
                definitions[i] =
                    (scenario_definition_t){ids[i], (timeout - 30) * 1000, body, configs + i, NULL};
            }
            scenario_options_t options = {.build_directory = paths[8], .data_directory = paths[9]};
            const char *selection[] = {argv[1], keep ? "--keep-work" : NULL};
            result = scenario_registry_main(definitions, COUNT, keep ? 2 : 1, selection, &options);
        }
        for (size_t i = 2; i < 12; i++)
            FREE(paths[i]);
    } else
        fputs("usage: module_musl_scenarios ID GOAT INSPECT MERGE SELECTOR CC FAMILY BUILD "
              "DATA FORMAT_ROOT [--destination DIR] [--keep-work] [--timeout-seconds 31..900]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
