/** @file inspect_probe_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real native inspector probes, independent owner and janitor lifetime contracts (#373).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "test/support/format_json.h"
#include "test/support/inspect_probe_fixture.h"
#include "test/support/inspect_report.h"
#include "test/support/language_execution.h"
#include "test/support/module_loading_owner.h"
#include "test/support/process_watch.h"
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
    const char *inspect, *builder, *cc, *host, *merge, *source, *format, *kind;
} config_t;

typedef struct {
    scenario_run_t *r;
    config_t *c;
    ip_fixture_t *f;
    const char *root, *worker, *original, *valid;
    unsigned serial;
} suite_t;

typedef struct {
    suite_t *s;
    const char *root, *markers, *exe;
    const char *environment[24];
    loading_owner_t *owner;
    process_watch_t **descendants;
    size_t descendant_count;
} case_t;

typedef struct {
    jval_t *json;
    double elapsed;
} report_t;

typedef struct {
    process_watch_t *watch;
    const char *gate;
} pin_t;

static jval_t *get(jval_t *v, const char *k) {
    return yyjson_mut_obj_get(v, k);
}

static bool eq(jval_t *v, const char *s) {
    return yyjson_mut_is_str(v) && yyjson_mut_get_len(v) == strlen(s)
           && !memcmp(yyjson_mut_get_str(v), s, strlen(s));
}

static const char *str(scenario_run_t *r, jval_t *v) {
    const char *s = yyjson_mut_get_str(v);
    REQUIRE(s && strlen(s) == yyjson_mut_get_len(v));
    return s;
}

static bool free_doc(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool free_plain(void *p) {
    free(p);
    return true;
}

static const char *string(scenario_run_t *r, const char *fmt, ...) {
    va_list a, b;
    va_start(a, fmt);
    va_copy(b, a);
    int n = vsnprintf(NULL, 0, fmt, b);
    va_end(b);
    REQUIRE(n >= 0);
    char *s = language_owned(r, ALLOC((size_t)n + 1));
    vsnprintf(s, (size_t)n + 1, fmt, a);
    va_end(a);
    return s;
}

static bytes_t read_bytes(scenario_run_t *r, const char *p) {
    size_t n;
    const char *d = language_read(r, p, &n);
    return (bytes_t){d, n};
}

static bool exists(scenario_run_t *r, const char *p) {
    fs_info_t info;
    fs_error_t e = {0};
    bool ok = inspect_fs_path(p, true, &info, &e), missing = e.status == FS_NOT_FOUND;
    clear_fs_error(&e);
    REQUIRE(ok || missing);
    return ok;
}

static void directory(scenario_run_t *r, const char *p) {
    if (exists(r, p))
        return;
    const char *slash = strrchr(p, '/');
    if (slash) {
        const char *parent = string(r, "%.*s", (int)(slash - p), p);
        if (*parent)
            directory(r, parent);
    }
    language_directory(r, p);
}

static void write_bytes(scenario_run_t *r, const char *p, bytes_t b) {
    const char *slash = strrchr(p, '/');
    if (slash)
        directory(r, string(r, "%.*s", (int)(slash - p), p));
    language_write(r, p, b.data, b.size);
}

static void touch(scenario_run_t *r, const char *p) {
    language_write(r, p, "", 0);
}

static const char *canonical(scenario_run_t *r, const char *p) {
    fs_error_t e = {0};
    char *q = canonical_fs_path(p, &e);
    clear_fs_error(&e);
    return language_owned(r, q);
}

static jval_t *parse(scenario_run_t *r, bytes_t b) {
    char e[512];
    yyjson_mut_doc *d = inspect_report_parse(b.data, b.size, e, sizeof(e));
    scenario_require(r, d != NULL, e);
    scenario_defer(r, free_doc, d);
    return yyjson_mut_doc_get_root(d);
}

static void put_json(scenario_run_t *r, const char *p, jval_t *v) {
    size_t n;
    char *b = format_json_canonical(v, &n);
    REQUIRE(b);
    scenario_defer(r, free_plain, b);
    write_bytes(r, p, (bytes_t){b, n});
}

static uint64_t deadline(scenario_run_t *r, uint64_t timeout) {
    uint64_t now = child_monotonic_ms(), end = scenario_run_deadline(r),
             remaining = end > now ? end - now : 0;
    if (remaining > timeout)
        remaining = timeout;
    uint64_t watch = process_watch_now_ms();
    return watch > UINT64_MAX - remaining ? UINT64_MAX : watch + remaining;
}

static bool cleanup_pin(void *arg) {
    pin_t *p = arg;
    unsigned long error = 0;
    bool ok = true, alive = false;
    if (p->watch) {
        if (p->gate) {
            FILE *file = FOPEN(p->gate, "wb");
            fs_error_t e = {0};
            if (!file || !close_fs_file(file, p->gate, &e))
                ok = false;
            clear_fs_error(&e);
            if (process_watch_wait(p->watch, process_watch_now_ms() + 20000, &error)
                != PROCESS_WATCH_OK)
                ok = false;
        }
        if (process_watch_alive(p->watch, &alive, &error) != PROCESS_WATCH_OK)
            ok = false;
        if (alive) {
            ok = false;
            process_watch_kill(p->watch, &error);
            if (process_watch_wait(p->watch, process_watch_now_ms() + 15000, &error)
                != PROCESS_WATCH_OK)
                ok = false;
        }
        if (process_watch_close(&p->watch, &error) != PROCESS_WATCH_OK)
            ok = false;
    }
    free(p);
    return ok;
}

static pin_t *pin(scenario_run_t *r, const char *text) {
    char *end;
    unsigned long long pid = strtoull(text, &end, 10);
    REQUIRE(pid && end != text && !*end);
    unsigned long error = 0;
    pin_t *p = calloc(1, sizeof(*p));
    REQUIRE(p);
    p->watch = process_watch_open(pid, &error);
    if (!p->watch) {
        free(p);
        REQUIRE(false);
    }
    scenario_defer(r, cleanup_pin, p);
    return p;
}

static void pin_alive(scenario_run_t *r, pin_t *p) {
    unsigned long error = 0;
    bool alive = false;
    REQUIRE(process_watch_alive(p->watch, &alive, &error) == PROCESS_WATCH_OK && alive);
}

static void pin_wait(scenario_run_t *r, pin_t *p, uint64_t timeout) {
    unsigned long error = 0;
    REQUIRE(process_watch_wait(p->watch, deadline(r, timeout), &error) == PROCESS_WATCH_OK);
    REQUIRE(process_watch_close(&p->watch, &error) == PROCESS_WATCH_OK);
}

static void set_env(case_t *c, const char *name, const char *value) {
    scenario_run_t *r = c->s->r;
    size_t n = strlen(name), index = 0;
    for (; c->environment[index]; index++)
        if (!strncmp(c->environment[index], name, n) && c->environment[index][n] == '=')
            break;
    REQUIRE(index < 23);
    c->environment[index] = string(r, "%s=%s", name, value);
}

static const char *const *environment(case_t *c) {
    scenario_run_t *r = c->s->r;
    const char *const *base = language_environment_override(r, "TMPDIR", c->root);
    size_t n = 0, m = 0;
    while (base[n])
        n++;
    while (c->environment[m])
        m++;
    const char **out = language_owned(r, CALLOC((n + m + 1) * sizeof(*out)));
    size_t used = 0;
    for (size_t i = 0; i < n; i++) {
        bool replace = false;
        for (size_t j = 0; j < m; j++) {
            const char *eqsign = strchr(c->environment[j], '=');
            size_t length = (size_t)(eqsign - c->environment[j]);
#ifdef _WIN32
            replace |= !_strnicmp(base[i], c->environment[j], length) && base[i][length] == '=';
#else
            replace |= !strncmp(base[i], c->environment[j], length) && base[i][length] == '=';
#endif
        }
        if (!replace)
            out[used++] = base[i];
    }
    for (size_t j = 0; j < m; j++)
        out[used++] = c->environment[j];
    return out;
}

static case_t *new_case(suite_t *s, const char *name) {
    scenario_run_t *r = s->r;
    case_t *c = language_owned(r, CALLOC(sizeof(*c)));
    c->s = s;
    c->exe = s->c->inspect;
    c->root = language_join(r, s->root, string(r, "case-%s", name));
    directory(r, c->root);
    c->markers = language_join(r, c->root, "markers");
    directory(r, c->markers);
    set_env(c, "TMPDIR", c->root);
    set_env(c, "TMP", c->root);
    set_env(c, "TEMP", c->root);
    set_env(c, "GOAT_PROBE_TEST_MARKERS", c->markers);
    set_env(c, "GOAT_PROBE_TEST_WORKER", s->worker);
    return c;
}

static const char *mark(case_t *c, const char *name) {
    return language_join(c->s->r, c->markers, name);
}

static const char *events(case_t *c) {
    scenario_run_t *r = c->s->r;
    const char *p = mark(c, "events");
    if (!exists(r, p))
        return "";
    bytes_t b = read_bytes(r, p);
    char *out = language_owned(r, ALLOC(b.size + 1));
    size_t n = 0;
    for (size_t i = 0; i < b.size; i++) {
        if (((const char *)b.data)[i] == '\r' && i + 1 < b.size
            && ((const char *)b.data)[i + 1] == '\n')
            continue;
        out[n++] = ((const char *)b.data)[i];
    }
    out[n] = 0;
    return out;
}

static void no_runs(case_t *c) {
    scenario_run_t *r = c->s->r;
    fs_entries_t found = {0};
    fs_error_t e = {0};
    bool ok = list_fs_directory(c->root, &found, &e);
    clear_fs_error(&e);
    bool none = true;
    for (size_t i = 0; i < found.count; i++)
        none &= strncmp(found.names[i], "goat-run-", 9) != 0;
    clear_fs_entries(&found);
    REQUIRE(ok && none);
}

typedef struct {
    case_t *c;
    const char *path;
    bool absent;
    char *text;
} marker_condition_t;

static process_watch_result_t marker_ready(void *arg, unsigned long *error) {
    marker_condition_t *m = arg;
    if (m->c && m->c->owner)
        loading_owner_command_poll(m->c->owner);
    fs_error_t e = {0};
    if (m->absent) {
        fs_info_t info;
        bool found = inspect_fs_path(m->path, true, &info, &e);
        bool missing = e.status == FS_NOT_FOUND;
        bool pending = false;
#ifdef _WIN32
        pending = e.native_code == 5 || e.native_code == 32;
#endif
        *error = e.native_code;
        clear_fs_error(&e);
        return found || pending ? PROCESS_WATCH_TIMEOUT
               : missing        ? PROCESS_WATCH_OK
                                : PROCESS_WATCH_ERROR;
    }
    void *data = NULL;
    size_t size = 0;
    bool ok = read_fs_regular_file(m->path, 4096, &data, &size, &e);
    bool missing = e.status == FS_NOT_FOUND;
    *error = e.native_code;
    clear_fs_error(&e);
    if (!ok)
        return missing ? PROCESS_WATCH_TIMEOUT : PROCESS_WATCH_ERROR;
    while (size && ((char *)data)[size - 1] <= 32)
        size--;
    if (!size) {
        FREE(data);
        return PROCESS_WATCH_TIMEOUT;
    }
    ((char *)data)[size] = 0;
    m->text = data;
    return PROCESS_WATCH_OK;
}

static const char *wait_marker(case_t *c, const char *p, uint64_t timeout) {
    scenario_run_t *r = c->s->r;
    marker_condition_t m = {c, p, false, NULL};
    unsigned long e = 0;
    REQUIRE(process_watch_until(marker_ready, &m, deadline(r, timeout), &e) == PROCESS_WATCH_OK);
    return language_owned(r, m.text);
}

static void wait_absent(case_t *c, const char *p, uint64_t timeout) {
    scenario_run_t *r = c->s->r;
    marker_condition_t m = {c, p, true, NULL};
    unsigned long e = 0;
    REQUIRE(process_watch_until(marker_ready, &m, deadline(r, timeout), &e) == PROCESS_WATCH_OK);
}

static loading_owner_t *
start(case_t *c, const char *archive, const char *const *options, bool keep_input) {
    scenario_run_t *r = c->s->r;
    size_t n = 0;
    while (options && options[n])
        n++;
    const char **args = language_owned(r, CALLOC((n + 4) * sizeof(*args)));
    args[0] = c->exe;
    args[1] = "--json";
    for (size_t i = 0; i < n; i++)
        args[i + 2] = options[i];
    args[n + 2] = archive;
    const char *input = keep_input ? "" : "parent input must not leak";
    c->owner = loading_owner_command_start(r,
                                           c->s->root,
                                           string(r, "inspect-%u", ++c->s->serial),
                                           args,
                                           environment(c),
                                           input,
                                           strlen(input),
                                           !keep_input);
    return c->owner;
}

static report_t finish_report(case_t *c, int expected, uint64_t started, bool close_input) {
    scenario_run_t *r = c->s->r;
    loading_owner_command_wait(c->owner, expected, 25000, close_input);
    loading_bytes_t out = loading_owner_output(c->owner), err = loading_owner_error(c->owner);
    REQUIRE(err.size == 0);
    jval_t *report = parse(r, (bytes_t){out.data, out.size}),
           *schema = parse(
               r,
               read_bytes(r, language_join(r, c->s->c->format, "inspect-report.schema.json")));
    char error[512];
    scenario_require(
        r,
        inspect_report_schema(report, schema, schema, c->s->c->format, error, sizeof(error)),
        error);
    REQUIRE(yyjson_mut_get_sint(get(report, "exitCode")) == expected
            && yyjson_mut_get_bool(get(report, "success")) == (expected == 0));
    REQUIRE(yyjson_mut_is_uint(get(report, "reportVersion"))
            && yyjson_mut_get_uint(get(report, "reportVersion")) == 1);
    return (report_t){report, (child_monotonic_ms() - started) / 1000.0};
}

static report_t
report(case_t *c, const char *archive, const char *mode, int expected, const char *const *options) {
    scenario_run_t *r = c->s->r;
    set_env(c, "GOAT_PROBE_TEST_MODE", mode);
    uint64_t started = child_monotonic_ms();
    start(c, archive, options, false);
    pin_t *descendant = NULL;
    if (!strncmp(mode, "tree", 4)) {
        descendant = pin(r, wait_marker(c, mark(c, "descendant"), 10000));
        touch(r, mark(c, "descendant-pinned"));
    }
    report_t value = finish_report(c, expected, started, true);
    if (descendant)
        pin_wait(r, descendant, 10000);
    return value;
}

static jval_t *loading(scenario_run_t *r, jval_t *report) {
    jval_t *chosen = NULL;
    size_t count = 0, i, n;
    jval_t *m;
    yyjson_mut_arr_foreach(get(report, "modules"), i, n, m) {
        size_t j, k;
        jval_t *v;
        yyjson_mut_arr_foreach(get(m, "native"),
                               j,
                               k,
                               v) if (eq(get(get(v, "compatibility"), "state"), "compatible")) {
            chosen = get(v, "loading");
            count++;
        }
    }
    REQUIRE(count == 1);
    return chosen;
}

static jval_t *checked_loading(case_t *c, report_t report) {
    scenario_run_t *r = c->s->r;
    jval_t *l = loading(r, report.json);
    REQUIRE(eq(get(l, "state"), "checked"));
    jval_t *cleanup = get(l, "cleanup");
    REQUIRE(eq(get(cleanup, "state"), "passed")
            && yyjson_mut_arr_size(get(cleanup, "remainingPaths")) == 0);
    no_runs(c);
    return get(l, "probe");
}

static void probe_state(scenario_run_t *r, jval_t *p, const char *state, const char *stage) {
    REQUIRE(eq(get(p, "state"), state));
    if (stage)
        REQUIRE(eq(get(p, "stage"), stage));
}

static void completed(scenario_run_t *r, jval_t *p) {
    const char *names[] = {"load", "descriptor", "symbols", "unload"};
    jval_t *a = get(p, "completedStages");
    REQUIRE(yyjson_mut_arr_size(a) == 4);
    for (size_t i = 0; i < 4; i++)
        REQUIRE(eq(yyjson_mut_arr_get(a, i), names[i]));
}

static void loaded_once(case_t *c, size_t expected) {
    scenario_run_t *r = c->s->r;
    const char *text = events(c), *p = text;
    size_t n = 0;
    while ((p = strstr(p, "loaded\n"))) {
        if (p == text || p[-1] == '\n')
            n++;
        p += 7;
    }
    REQUIRE(n == expected && !strstr(text, "called"));
}

static void capture(scenario_run_t *r, jval_t *probe, size_t size) {
    const char *streams[] = {"stdout", "stderr"};
    const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t n = size < 65536 ? size : 65536;
    for (size_t stream = 0; stream < 2; stream++) {
        jval_t *v = get(get(probe, "capture"), streams[stream]);
        char *expected = language_owned(r, ALLOC((n + 2) / 3 * 4 + 1));
        size_t p = 0;
        for (size_t i = 0; i < n; i += 3) {
            unsigned a = (unsigned)((i + (stream ? 97 : 0)) % 256),
                     b = i + 1 < n ? (unsigned)((i + 1 + (stream ? 97 : 0)) % 256) : 0,
                     c = i + 2 < n ? (unsigned)((i + 2 + (stream ? 97 : 0)) % 256) : 0;
            expected[p++] = alphabet[a >> 2];
            expected[p++] = alphabet[((a & 3) << 4) | (b >> 4)];
            expected[p++] = i + 1 < n ? alphabet[((b & 15) << 2) | (c >> 6)] : '=';
            expected[p++] = i + 2 < n ? alphabet[c & 63] : '=';
        }
        expected[p] = 0;
        REQUIRE(eq(get(v, "dataBase64"), expected)
                && yyjson_mut_get_bool(get(v, "truncated")) == (size > 65536));
    }
}

static yyjson_mut_doc *new_doc(scenario_run_t *r) {
    yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
    REQUIRE(d);
    scenario_defer(r, free_doc, d);
    return d;
}

static yyjson_mut_doc *copy_doc(scenario_run_t *r, jval_t *v) {
    yyjson_mut_doc *d = new_doc(r);
    jval_t *copy = yyjson_mut_val_mut_copy(d, v);
    REQUIRE(copy);
    yyjson_mut_doc_set_root(d, copy);
    return d;
}

static void set(scenario_run_t *r, yyjson_mut_doc *d, jval_t *obj, const char *key, jval_t *v) {
    REQUIRE(v && yyjson_mut_obj_put(obj, yyjson_mut_str(d, key), v));
}

static jval_t *aroot(ip_archive_t *a) {
    return yyjson_mut_doc_get_root(a->doc);
}

static jval_t *avariant(scenario_run_t *r, ip_archive_t *a) {
    jval_t *v = yyjson_mut_arr_get(get(aroot(a), "native"), 0);
    REQUIRE(v);
    return v;
}

static const char *profile_path(suite_t *s, yyjson_mut_doc *d, const char *name) {
    const char *p = language_join(s->r, s->root, name);
    put_json(s->r, p, yyjson_mut_doc_get_root(d));
    return p;
}

static yyjson_mut_doc *current_profile(suite_t *s) {
    scenario_run_t *r = s->r;
    case_t *c = new_case(s, "current-profile");
    report_t result = report(c, s->valid, "valid", 0, NULL);
    jval_t *t = get(result.json, "target");
    yyjson_mut_doc *d = new_doc(r);
    jval_t *p = yyjson_mut_obj(d);
    REQUIRE(p);
    yyjson_mut_doc_set_root(d, p);
    const char *names[] = {"os", "architecture", "bits", "cpuFeatures"};
    for (size_t i = 0; i < 4; i++)
        set(r, d, p, names[i], yyjson_mut_val_mut_copy(d, get(t, names[i])));
    set(r,
        d,
        p,
        "endian",
        yyjson_mut_str(d, yyjson_mut_get_bool(get(t, "littleEndian")) ? "little" : "big"));
    set(r, d, p, "minimumOs", yyjson_mut_val_mut_copy(d, get(t, "osVersion")));
    jval_t *runtime = yyjson_mut_obj(d);
    set(r, d, runtime, "family", yyjson_mut_val_mut_copy(d, get(t, "runtime")));
    set(r, d, runtime, "minimumVersion", yyjson_mut_val_mut_copy(d, get(t, "runtimeVersion")));
    set(r, d, p, "runtime", runtime);
    return d;
}

static void foreign_minimum(scenario_run_t *r, yyjson_mut_doc *d) {
    jval_t *a = yyjson_mut_arr(d);
    REQUIRE(a && yyjson_mut_arr_add_uint(d, a, 999));
    set(r, d, yyjson_mut_doc_get_root(d), "minimumOs", a);
}

static yyjson_mut_doc *opposite_profile(scenario_run_t *r, yyjson_mut_doc *current) {
    yyjson_mut_doc *d = copy_doc(r, yyjson_mut_doc_get_root(current));
    jval_t *p = yyjson_mut_doc_get_root(d), *runtime = yyjson_mut_obj(d),
           *minos = yyjson_mut_arr(d), *version = yyjson_mut_arr(d);
    REQUIRE(minos && version && yyjson_mut_arr_add_uint(d, minos, 10)
            && yyjson_mut_arr_add_uint(d, version, 0));
#ifdef _WIN32
    const char *os = "linux", *family = "glibc";
#else
    const char *os = "windows", *family = "msvcrt";
#endif
    set(r, d, p, "os", yyjson_mut_str(d, os));
    set(r, d, p, "minimumOs", minos);
    set(r, d, runtime, "family", yyjson_mut_str(d, family));
    set(r, d, runtime, "minimumVersion", version);
    set(r, d, p, "runtime", runtime);
    return d;
}

static const char *
opposite_archive(suite_t *s, const char *original, jval_t *profile, const char *output) {
    scenario_run_t *r = s->r;
    ip_archive_t *a = ip_read(s->f, original);
    jval_t *v = avariant(r, a);
    const char *old = str(r, get(v, "library"));
    ip_part_t *part = ip_part(s->f, a, old);
#ifdef _WIN32
    const char *name = "native/opposite.so";
#else
    const char *name = "native/opposite.dll";
#endif
    part->name = name;
    set(r, a->doc, v, "library", yyjson_mut_str(a->doc, name));
    set(r, a->doc, v, "target", yyjson_mut_val_mut_copy(a->doc, profile));
    return ip_save(s->f, output, a);
}

static void all_loading(scenario_run_t *r, jval_t *json, const char *state, bool probe) {
    size_t i, n;
    jval_t *m;
    yyjson_mut_arr_foreach(get(json, "modules"), i, n, m) {
        size_t j, k;
        jval_t *v;
        yyjson_mut_arr_foreach(get(m, "native"), j, k, v) {
            jval_t *l = get(v, "loading");
            REQUIRE(eq(get(probe ? get(l, "probe") : l, "state"), state));
        }
    }
}

static jval_t *module_named(scenario_run_t *r, jval_t *report, const char *name) {
    size_t i, n;
    jval_t *m;
    yyjson_mut_arr_foreach(get(report, "modules"), i, n, m) if (eq(get(get(m, "metadata"), "name"),
                                                                   name)) return m;
    REQUIRE(false);
    return NULL;
}

static void same_archives(scenario_run_t *r, jval_t *a, jval_t *b) {
    jval_t *x = get(a, "modules"), *y = get(b, "modules");
    REQUIRE(yyjson_mut_arr_size(x) == yyjson_mut_arr_size(y));
    for (size_t i = 0; i < yyjson_mut_arr_size(x); i++)
        REQUIRE(format_json_equal(get(yyjson_mut_arr_get(x, i), "archive"),
                                  get(yyjson_mut_arr_get(y, i), "archive")));
}

static void basic_cases(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *kind = s->c->kind;
    case_t *c = new_case(s, kind);
    if (!strcmp(kind, "stdin")) {
        set_env(c, "GOAT_PROBE_TEST_MODE", "stdin");
        uint64_t begin = child_monotonic_ms();
        start(c, s->valid, NULL, true);
        loading_owner_command_wait(c->owner, 0, 15000, false);
        report_t value = finish_report(c, 0, begin, false);
        REQUIRE(yyjson_mut_get_bool(get(value.json, "success")));
        REQUIRE(exists(r, mark(c, "eof")) && !strstr(events(c), "called"));
        return;
    }
    if (!strcmp(kind, "text-output")) {
        set_env(c, "GOAT_PROBE_TEST_MODE", "output");
        set_env(c, "GOAT_PROBE_TEST_BYTES", "257");
        loading_owner_t *o = loading_owner_command_start(r,
                                                         s->root,
                                                         "text",
                                                         ARGS(s->c->inspect, s->valid),
                                                         environment(c),
                                                         NULL,
                                                         0,
                                                         true);
        loading_owner_command_wait(o, 0, 60000, true);
        loading_bytes_t out = loading_owner_output(o), err = loading_owner_error(o);
        char *combined = language_owned(r, ALLOC(out.size + err.size + 1));
        if (out.size)
            memcpy(combined, out.data, out.size);
        if (err.size)
            memcpy(combined + out.size, err.data, err.size);
        size_t size = out.size + err.size;
        combined[size] = 0;
        REQUIRE(!memchr(combined, 27, size) && !memchr(combined, 0, size));
        if (size) {
            string_value_t decoded = decode_utf8_n(combined, size);
            bool valid = decoded.data != NULL;
            FREE_STRING(decoded);
            REQUIRE(valid);
        }
        return;
    }
    const char *archive = s->valid, *mode = kind;
    int expected = 0;
    const char *state = "passed", *stage = "exit";
    const char *const timeout_options[] = {"--probe-timeout", "1", NULL};
    const char *const *options = NULL;
    if (!strcmp(kind, "success"))
        mode = "valid";
    else if (!strncmp(kind, "capture-", 8)) {
        set_env(c, "GOAT_PROBE_TEST_BYTES", kind + 8);
        mode = "output";
    } else if (!strcmp(kind, "default-timeout")) {
        mode = "load-hang";
        state = "timeout";
        stage = "load";
    } else if (!strncmp(kind, "descriptor-", 11) || !strcmp(kind, "symbol-missing")) {
        archive = ip_custom(s->f, s->original, kind);
        mode = "valid";
        expected = 1;
        state = "failed";
        stage = !strcmp(kind, "symbol-missing") ? "symbols" : "descriptor";
    } else if (!strcmp(kind, "load-error")) {
        ip_archive_t *a = ip_read(s->f, s->original);
        jval_t *v = avariant(r, a);
        ip_put(s->f, a, str(r, get(v, "library")), "not a native image\0", 19);
        archive = ip_save(s->f, language_join(r, s->root, "bad-image.gbin"), a);
        mode = "valid";
        state = "failed";
        stage = "load";
    } else if (!strncmp(kind, "fault-", 6)) {
        const char *fault = kind + 6;
        c->exe = s->c->host;
        set_env(c, "GOAT_INSPECT_PROBE_FAULT", fault);
        mode = "valid";
        options = timeout_options;
        state = "failed";
        stage = fault;
        if (!strcmp(fault, "start") || !strcmp(fault, "capture"))
            expected = 1;
        if (!strcmp(fault, "diagnostic"))
            stage = "unload";
        if (!strcmp(fault, "exit-nonzero")) {
            state = "crashed";
            stage = "exit";
        }
        if (!strcmp(fault, "exit-hang")) {
            state = "timeout";
            stage = "exit";
        }
    } else if (!strncmp(kind, "cleaner-start-", 14)) {
        c->exe = s->c->host;
        set_env(c, "GOAT_INSPECT_CLEANER_FAULT", kind + 14);
        report_t value = report(c, archive, "valid", 1, NULL);
        REQUIRE(!*events(c) && value.elapsed < 10);
        no_runs(c);
        return;
    } else {
        options = timeout_options;
        stage = !strncmp(kind, "unload-", 7) ? "unload" : "load";
        state = strstr(kind, "crash") ? "crashed" : strstr(kind, "hang") ? "timeout" : "failed";
    }
    report_t value = report(c, archive, mode, expected, options);
    jval_t *p = checked_loading(c, value);
    probe_state(r, p, state, stage);
    if (!strcmp(kind, "success")) {
        completed(r, p);
        REQUIRE(!strcmp(events(c), "loaded\nunloaded\n") && value.elapsed < 5);
    } else if (!strncmp(kind, "capture-", 8))
        capture(r, p, (size_t)strtoull(kind + 8, NULL, 10));
    else if (!strcmp(kind, "default-timeout"))
        REQUIRE(value.elapsed >= 4.5 && value.elapsed < 12);
    else if (!strncmp(kind, "descriptor-", 11) || !strcmp(kind, "symbol-missing")) {
        REQUIRE(yyjson_mut_arr_size(get(p, "diagnostics")) > 0
                && !strcmp(events(c), "loaded\nunloaded\n"));
    } else if (!strcmp(kind, "load-error"))
        REQUIRE(yyjson_mut_arr_size(get(p, "diagnostics")) > 0);
    else if (!strncmp(kind, "fault-", 6)) {
        if (!strcmp(kind, "fault-diagnostic")) {
            const char *message =
                str(r, get(yyjson_mut_arr_get(get(p, "diagnostics"), 0), "message"));
            REQUIRE(!strncmp(message, "\\xe2\\xff", 8));
        }
        if (!strncmp(kind, "fault-exit-", 11))
            completed(r, p);
    } else {
        REQUIRE(yyjson_mut_arr_size(get(p, "diagnostics")) > 0);
        loaded_once(c, 1);
        REQUIRE(value.elapsed < 12);
        if (!strcmp(kind, "output-hang")) {
            REQUIRE(yyjson_mut_get_bool(get(get(get(p, "capture"), "stdout"), "truncated"))
                    && yyjson_mut_get_bool(get(get(get(p, "capture"), "stderr"), "truncated")));
            REQUIRE(value.elapsed < 5);
        }
    }
}

static const char *node(suite_t *s, const char *name, const char *const *dependencies) {
    scenario_run_t *r = s->r;
    ip_archive_t *a = ip_read(s->f, s->original);
    set(r, a->doc, aroot(a), "name", yyjson_mut_strcpy(a->doc, name));
    if (dependencies && dependencies[0]) {
        jval_t *deps = yyjson_mut_arr(a->doc);
        REQUIRE(deps);
        for (size_t i = 0; dependencies[i]; i++) {
            ip_archive_t *d = ip_read(s->f, dependencies[i]);
            jval_t *identity = yyjson_mut_obj(a->doc);
            const char *keys[] = {"name", "version", "mainHash"},
                       *out[] = {"name", "version", "hash"};
            for (size_t j = 0; j < 3; j++)
                set(r,
                    a->doc,
                    identity,
                    out[j],
                    yyjson_mut_val_mut_copy(a->doc, get(aroot(d), keys[j])));
            REQUIRE(yyjson_mut_arr_append(deps, identity));
        }
        set(r, a->doc, aroot(a), "dependencies", deps);
    }
    const char *raw = ip_save(s->f, language_join(r, s->root, string(r, "%s-raw.gbin", name)), a);
    const char *native = ip_custom(s->f, raw, "valid");
    const char *destination = language_join(r, s->root, string(r, "modules/%s/1.gbin", name));
    write_bytes(r, destination, read_bytes(r, native));
    return destination;
}

static void hash_sets(scenario_run_t *r, jval_t *a, jval_t *b) {
    jval_t *x = get(a, "modules"), *y = get(b, "modules");
    REQUIRE(yyjson_mut_arr_size(x) == yyjson_mut_arr_size(y));
    for (size_t i = 0; i < yyjson_mut_arr_size(x); i++) {
        jval_t *hash = get(get(yyjson_mut_arr_get(x, i), "metadata"), "mainHash");
        bool found = false;
        for (size_t j = 0; j < yyjson_mut_arr_size(y); j++)
            found |=
                format_json_equal(hash, get(get(yyjson_mut_arr_get(y, j), "metadata"), "mainHash"));
        REQUIRE(found);
    }
}

static void graph_cases(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *kind = s->c->kind, *modules = language_join(r, s->root, "modules");
    const char *leaf = node(s, "diamond-leaf", NULL), *left = node(s, "diamond-left", ARGS(leaf)),
               *right = node(s, "diamond-right", ARGS(leaf)),
               *graph = node(s, "diamond-root", ARGS(left, right));
    case_t *base = new_case(s, "diamond");
    report_t initial = report(base, graph, "valid", 0, ARGS("--module-dir", modules));
    REQUIRE(yyjson_mut_arr_size(get(initial.json, "modules")) == 4
            && !strcmp(events(base),
                       "loaded\nunloaded\nloaded\nunloaded\nloaded\nunloaded\nloaded\nunloaded\n"));
    all_loading(r, initial.json, "passed", true);
    if (!strcmp(kind, "diamond"))
        return;
    bytes_t leaf_bytes = read_bytes(r, leaf);
    if (!strcmp(kind, "diamond-foreign") || strstr(kind, "candidate")) {
        yyjson_mut_doc *profile = current_profile(s);
        foreign_minimum(r, profile);
        const char *profile_file = profile_path(s, profile, "current-target.json");
        if (!strcmp(kind, "diamond-foreign")) {
            case_t *c = new_case(s, kind);
            report_t result = report(c,
                                     graph,
                                     "valid",
                                     0,
                                     ARGS("--module-dir", modules, "--target", profile_file));
            same_archives(r, initial.json, result.json);
            size_t i, n;
            jval_t *m;
            yyjson_mut_arr_foreach(get(result.json, "modules"), i, n, m)
                REQUIRE(eq(get(m, "validation"), "passed"));
            all_loading(r, result.json, "not_checked", false);
            REQUIRE(!*events(c));
            return;
        }
        yyjson_mut_doc *opposite = opposite_profile(r, profile);
        const char *opposite_file = profile_path(s, opposite, "opposite-os-target.json");
        const char *neighbor =
            language_join(r, s->root, "modules/diamond-root/modules/diamond-leaf/1.gbin");
        write_bytes(r, neighbor, leaf_bytes);
        opposite_archive(s, leaf, yyjson_mut_doc_get_root(opposite), leaf);
        ip_archive_t *a = ip_read(s->f, leaf), *b = ip_read(s->f, neighbor);
        REQUIRE(format_json_equal(get(aroot(a), "mainHash"), get(aroot(b), "mainHash")));
        case_t *current = new_case(s, "first-candidate-current");
        report_t current_result = report(current, graph, "valid", 0, ARGS("--module-dir", modules));
        jval_t *shared = module_named(r, current_result.json, "diamond-leaf");
        REQUIRE(eq(get(shared, "archive"), canonical(r, neighbor)));
        REQUIRE(eq(get(get(yyjson_mut_arr_get(get(shared, "native"), 0), "compatibility"), "state"),
                   "compatible"));
        REQUIRE(
            !strcmp(events(current),
                    "loaded\nunloaded\nloaded\nunloaded\nloaded\nunloaded\nloaded\nunloaded\n"));
        if (!strcmp(kind, "first-candidate-current"))
            return;
        case_t *foreign = new_case(s, "first-candidate-foreign");
        report_t result = report(foreign,
                                 graph,
                                 "valid",
                                 0,
                                 ARGS("--module-dir", modules, "--target", opposite_file));
        shared = module_named(r, result.json, "diamond-leaf");
        REQUIRE(eq(get(shared, "archive"), canonical(r, neighbor)));
        REQUIRE(eq(get(get(yyjson_mut_arr_get(get(shared, "native"), 0), "compatibility"), "state"),
                   "incompatible"));
        REQUIRE(!*events(foreign));
        same_archives(r, current_result.json, result.json);
        if (!strcmp(kind, "first-candidate-foreign"))
            return;
        language_remove(r, neighbor);
        case_t *later = new_case(s, "later-candidate-foreign");
        set_env(later, "GOAT_MODULE_PATH", modules);
        result = report(later, graph, "valid", 0, ARGS("--target", opposite_file));
        shared = module_named(r, result.json, "diamond-leaf");
        REQUIRE(eq(get(shared, "archive"), canonical(r, leaf)));
        jval_t *v = yyjson_mut_arr_get(get(shared, "native"), 0);
        REQUIRE(eq(get(get(v, "compatibility"), "state"), "compatible")
                && eq(get(get(v, "loading"), "state"), "not_checked") && !*events(later));
        return;
    }
    if (!strcmp(kind, "invalid-graph")) {
        write_bytes(r, leaf, (bytes_t){"corrupted reachable archive", 27});
        case_t *c = new_case(s, kind);
        report_t result = report(c, graph, "valid", 1, ARGS("--module-dir", modules));
        REQUIRE(!*events(c));
        all_loading(r, result.json, "not_checked", false);
        return;
    }
    language_remove(r, leaf);
    case_t *c = new_case(s, "missing-transitive");
    set_env(c, "GOAT_MODULE_PATH", modules);
    report_t missing = report(c, graph, "valid", 1, NULL);
    REQUIRE(!*events(c));
    all_loading(r, missing.json, "not_checked", false);
    const char *merged = language_join(r, s->root, "repacked diamond root.gbin");
    child_options_t options = {.executable = s->c->merge,
                               .arguments = ARGS(s->c->merge, merged, graph, graph),
                               .environment = environment(c),
                               .empty_stdin = true};
    scenario_command_result_t result = scenario_command_capture(r, &options);
    REQUIRE(result.exit_code == 0 && !result.err_size && !*events(c));
    const char *out = result.out;
    size_t line_size = result.out_size;
    if (line_size && out[line_size - 1] == '\n')
        line_size--;
    if (line_size && out[line_size - 1] == '\r')
        line_size--;
    REQUIRE(line_size > 0 && !memchr(out, '\n', line_size) && !memchr(out, '\r', line_size));
    REQUIRE(!(line_size == 2 && !memcmp(out, "49", 2)));
    ip_archive_t *before = ip_read(s->f, graph), *after = ip_read(s->f, merged);
    ip_normalize(s->f, before);
    REQUIRE(format_json_equal(aroot(before), aroot(after))
            && format_json_equal(get(aroot(before), "mainHash"), get(aroot(after), "mainHash")));
    REQUIRE(before->count == after->count);
    for (size_t i = 0; i < before->count; i++) {
        ip_part_t *a = before->parts + i, *b = ip_part(s->f, after, a->name);
        scenario_require_bytes(r,
                               "merge preserves member bytes",
                               a->data,
                               a->size,
                               b->data,
                               b->size,
                               false);
    }
    no_runs(c);
    if (!strcmp(kind, "missing-transitive"))
        return;
    case_t *still = new_case(s, "repacked-still-missing");
    set_env(still, "GOAT_MODULE_PATH", modules);
    report(still, merged, "valid", 1, NULL);
    REQUIRE(!*events(still));
    if (!strcmp(kind, "repacked-still-missing"))
        return;
    REQUIRE(!strcmp(kind, "repacked-complete"));
    write_bytes(r, leaf, leaf_bytes);
    case_t *complete = new_case(s, kind);
    report_t repacked = report(complete, merged, "valid", 0, ARGS("--module-dir", modules));
    REQUIRE(yyjson_mut_arr_size(get(repacked.json, "modules")) == 4
            && !strcmp(events(complete),
                       "loaded\nunloaded\nloaded\nunloaded\nloaded\nunloaded\nloaded\nunloaded\n"));
    hash_sets(r, initial.json, repacked.json);
    hash_sets(r, repacked.json, initial.json);
    all_loading(r, repacked.json, "passed", true);
}

static void target_cases(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *kind = s->c->kind;
    yyjson_mut_doc *profile = current_profile(s);
    const char *profile_file = profile_path(s, profile, "current-target.json");
    case_t *c = new_case(s, "explicit-current-probe");
    report_t result = report(c, s->valid, "valid", 0, ARGS("--target", profile_file));
    jval_t *p = checked_loading(c, result);
    REQUIRE(eq(get(p, "state"), "passed") && !strcmp(events(c), "loaded\nunloaded\n"));
    if (!strcmp(kind, "explicit-current"))
        return;
    foreign_minimum(r, profile);
    profile_file = profile_path(s, profile, "current-target.json");
    if (!strcmp(kind, "foreign")) {
        c = new_case(s, kind);
        result = report(c, s->valid, "valid", 0, ARGS("--target", profile_file));
        REQUIRE(!*events(c));
        all_loading(r, result.json, "not_checked", false);
        return;
    }
    REQUIRE(!strcmp(kind, "opposite-os-compatible"));
    yyjson_mut_doc *opposite = opposite_profile(r, profile);
    const char *opposite_file = profile_path(s, opposite, "opposite-os-target.json"),
               *archive = opposite_archive(s,
                                           s->valid,
                                           yyjson_mut_doc_get_root(opposite),
                                           language_join(r, s->root, "opposite-os.gbin"));
    c = new_case(s, kind);
    set_env(c, "PATH", "");
    set_env(c, "CC", "no-foreign-compiler");
    set_env(c, "CXX", "no-foreign-compiler");
    result = report(c, archive, "valid", 0, ARGS("--target", opposite_file));
    REQUIRE(format_json_equal(get(get(result.json, "target"), "os"),
                              get(yyjson_mut_doc_get_root(opposite), "os")));
    jval_t *m = yyjson_mut_arr_get(get(result.json, "modules"), 0),
           *v = yyjson_mut_arr_get(get(m, "native"), 0);
    REQUIRE(eq(get(get(v, "compatibility"), "state"), "compatible")
            && eq(get(get(v, "loading"), "state"), "not_checked") && !*events(c));
    no_runs(c);
}

typedef struct {
    const char *path;
} gate_t;

static bool release_gate(void *argument) {
    gate_t *g = argument;
    fs_error_t e = {0};
    FILE *file = FOPEN(g->path, "wb");
    bool ok = file && close_fs_file(file, g->path, &e);
    clear_fs_error(&e);
    free(g);
    return ok;
}

static void cleanup_timeout(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *mode = s->c->kind + 16;
    const char *state = !strcmp(mode, "valid")            ? "passed"
                        : !strcmp(mode, "load-exit-zero") ? "failed"
                        : !strcmp(mode, "load-crash")     ? "crashed"
                                                          : "timeout";
    case_t *c = new_case(s, s->c->kind);
    c->exe = s->c->host;
    const char *gate = mark(c, "allow-cleanup");
    gate_t *g = calloc(1, sizeof(*g));
    REQUIRE(g);
    g->path = gate;
    scenario_defer(r, release_gate, g);
    set_env(c, "GOAT_INSPECT_PROBE_CLEANUP_GATE", gate);
    set_env(c, "GOAT_INSPECT_PROBE_CLEANUP_MARKER", mark(c, "cleanup-started"));
    set_env(c, "GOAT_INSPECT_CLEANER_PID", mark(c, "cleaner.pid"));
    report_t value = report(c, s->valid, mode, 1, ARGS("--probe-timeout", "1"));
    loaded_once(c, 1);
    REQUIRE(value.elapsed >= 4 && value.elapsed < 15);
    pin_t *cleaner = pin(r, wait_marker(c, mark(c, "cleaner.pid"), 1000));
    cleaner->gate = gate;
    pin_alive(r, cleaner);
    jval_t *l = loading(r, value.json);
    probe_state(r, get(l, "probe"), state, NULL);
    jval_t *cleanup = get(l, "cleanup");
    REQUIRE(eq(get(cleanup, "state"), "failed")
            && yyjson_mut_arr_size(get(cleanup, "diagnostics")) > 0);
    const char *temporary = str(r, get(cleanup, "temporaryDirectory"));
    const char *absolute = canonical(r, temporary);
    REQUIRE(absolute && !strcmp(absolute, temporary) && exists(r, temporary));
    REQUIRE(!strcmp(temporary, wait_marker(c, mark(c, "cleanup-started"), 1000)));
    REQUIRE(yyjson_mut_arr_size(get(cleanup, "remainingPaths")) == 0);
    touch(r, gate);
    wait_absent(c, temporary, 15000);
    pin_wait(r, cleaner, 15000);
    value = report(c, s->valid, mode, 0, ARGS("--probe-timeout", "1"));
    probe_state(r, checked_loading(c, value), state, NULL);
    loaded_once(c, 2);
}

static void owner_cases(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *kind = s->c->kind;
    case_t *c = new_case(s, kind);
    if (!strcmp(kind, "owner-killed-before-extraction")) {
        c->exe = s->c->host;
        set_env(c, "GOAT_INSPECT_PROBE_READY_MARKER", mark(c, "ready"));
        set_env(c, "GOAT_INSPECT_PROBE_READY_GATE", mark(c, "admit-extraction"));
        start(c, s->valid, NULL, false);
        const char *path = wait_marker(c, mark(c, "ready"), 10000);
        REQUIRE(exists(r, path) && !*events(c));
        fs_entries_t entries = {0};
        fs_error_t e = {0};
        bool ok = list_fs_directory(path, &entries, &e), empty = entries.count == 0;
        clear_fs_entries(&entries);
        clear_fs_error(&e);
        REQUIRE(ok && empty);
        loading_owner_command_kill(c->owner, 15000);
        wait_absent(c, path, 15000);
        return;
    }
    if (!strcmp(kind, "owner-killed")) {
        set_env(c, "GOAT_PROBE_TEST_MODE", "tree-hang");
        start(c, s->valid, ARGS("--probe-timeout", "60"), false);
        pin_t *descendant = pin(r, wait_marker(c, mark(c, "descendant"), 10000));
        touch(r, mark(c, "descendant-pinned"));
        fs_entries_t entries = {0};
        fs_error_t e = {0};
        bool ok = list_fs_directory(c->root, &entries, &e);
        clear_fs_error(&e);
        REQUIRE(ok);
        const char **paths = language_owned(r, CALLOC((entries.count + 1) * sizeof(*paths)));
        size_t count = 0;
        for (size_t i = 0; i < entries.count; i++)
            if (!strncmp(entries.names[i], "goat-run-", 9))
                paths[count++] = language_join(r, c->root, entries.names[i]);
        clear_fs_entries(&entries);
        REQUIRE(count > 0);
        loading_owner_command_kill(c->owner, 15000);
        pin_wait(r, descendant, 15000);
        for (size_t i = 0; i < count; i++)
            wait_absent(c, paths[i], 15000);
        return;
    }
    case_t *independent = new_case(s, "independent");
    independent->owner = loading_owner_command_start(r,
                                                     s->root,
                                                     "independent-worker",
                                                     ARGS(s->worker),
                                                     environment(independent),
                                                     NULL,
                                                     0,
                                                     true);
    pin_t *unrelated = pin(r, wait_marker(independent, mark(independent, "descendant"), 10000));
    report_t value = report(c, s->valid, kind, 0, ARGS("--probe-timeout", "1"));
    const char *state = !strcmp(kind, "tree")         ? "passed"
                        : !strcmp(kind, "tree-crash") ? "crashed"
                                                      : "timeout";
    probe_state(r, checked_loading(c, value), state, NULL);
    pin_alive(r, unrelated);
    loading_owner_command_kill(independent->owner, 10000);
    pin_wait(r, unrelated, 10000);
}

static void body(scenario_run_t *r, void *argument) {
    config_t *c = argument;
    suite_t s = {.r = r, .c = c, .root = scenario_work_directory(scenario_run_context(r))};
    s.f = ip_create(r,
                    c->builder,
                    c->cc,
                    c->source,
                    language_join(r, c->format, "schema-source.json"));
#ifdef _WIN32
    s.worker = language_join(r, s.root, "worker.exe");
#else
    s.worker = language_join(r, s.root, "worker");
#endif
    ip_compile(s.f,
               language_join(r, c->source, "test/src/component/inspect_probe_worker.c"),
               s.worker,
               false);
    s.original = ip_build(s.f, "probe");
    s.valid = ip_custom(s.f, s.original, "valid");
    if (!strncmp(c->kind, "cleanup-timeout-", 16))
        cleanup_timeout(&s);
    else if (!strncmp(c->kind, "owner-killed", 12) || !strncmp(c->kind, "tree", 4))
        owner_cases(&s);
    else if (!strncmp(c->kind, "diamond", 7) || strstr(c->kind, "candidate")
             || !strcmp(c->kind, "missing-transitive") || !strncmp(c->kind, "repacked-", 9)
             || !strcmp(c->kind, "invalid-graph"))
        graph_cases(&s);
    else if (!strcmp(c->kind, "explicit-current") || !strcmp(c->kind, "foreign")
             || !strcmp(c->kind, "opposite-os-compatible"))
        target_cases(&s);
    else
        basic_cases(&s);
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
    if (argc == 11 || (argc == 12 && !strcmp(argv[11], "--keep-work"))) {
        char *paths[11] = {0};
        bool valid_paths = true;
        for (size_t i = 2; i <= 10; i++) {
            fs_error_t e = {0};
            if (i == 4 && !strchr(argv[i], '/') && !strchr(argv[i], '\\')) {
                paths[i] = ALLOC(strlen(argv[i]) + 1);
                strcpy(paths[i], argv[i]);
            } else
                paths[i] = absolute_fs_path(argv[i], &e);
            clear_fs_error(&e);
            valid_paths &= paths[i] != NULL;
        }
        const char *names[] = {"success",
                               "capture-0",
                               "capture-65536",
                               "capture-65537",
                               "capture-262144",
                               "stdin",
                               "load-crash",
                               "load-hang",
                               "output-hang",
                               "unload-crash",
                               "unload-hang",
                               "load-exit-zero",
                               "default-timeout",
                               "descriptor-missing",
                               "descriptor-mismatch",
                               "descriptor-overflow",
                               "symbol-missing",
                               "load-error",
                               "explicit-current",
                               "foreign",
                               "opposite-os-compatible",
                               "diamond",
                               "diamond-foreign",
                               "first-candidate-current",
                               "first-candidate-foreign",
                               "later-candidate-foreign",
                               "missing-transitive",
                               "repacked-still-missing",
                               "repacked-complete",
                               "invalid-graph",
                               "text-output",
                               "fault-start",
                               "fault-capture",
                               "fault-unload",
                               "fault-diagnostic",
                               "fault-exit-nonzero",
                               "fault-exit-hang",
                               "cleaner-start-exit",
                               "cleaner-start-silent",
                               "cleanup-timeout-valid",
                               "cleanup-timeout-load-exit-zero",
                               "cleanup-timeout-load-crash",
                               "cleanup-timeout-load-hang",
                               "owner-killed-before-extraction",
                               "tree",
                               "tree-crash",
                               "tree-hang",
                               "owner-killed"};

        enum { COUNT = sizeof(names) / sizeof(names[0]) };

        config_t configs[COUNT];
        scenario_definition_t definitions[COUNT];
        char ids[COUNT][96];
        for (size_t i = 0; i < COUNT; i++) {
            snprintf(ids[i], sizeof(ids[i]), "integration.inspect_probe.%s", names[i]);
            configs[i] = (config_t){paths[2],
                                    paths[3],
                                    paths[4],
                                    paths[5],
                                    paths[6],
                                    paths[9],
                                    paths[10],
                                    names[i]};
            definitions[i] = (scenario_definition_t){ids[i], 180000, body, configs + i, NULL};
        }
        scenario_options_t options = {.build_directory = paths[7], .data_directory = paths[8]};
        const char *selection[] = {argv[1], argc == 12 ? "--keep-work" : NULL};
        if (valid_paths)
            result =
                scenario_registry_main(definitions, COUNT, argc == 12 ? 2 : 1, selection, &options);
        for (size_t i = 2; i <= 10; i++)
            FREE(paths[i]);
    } else
        fputs("usage: inspect_probe_scenarios ID INSPECT BUILDER CC HOST MERGE BUILD DATA "
              "SOURCE_ROOT FORMAT_ROOT [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
