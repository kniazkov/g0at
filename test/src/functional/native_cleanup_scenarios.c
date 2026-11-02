/** @file native_cleanup_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent native owner/compiler/cleaner observations (#381).
 */
#ifndef _WIN32
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/module_loading_owner.h"
#include "test/support/registry.h"

#include <ctype.h>
#include <errno.h>
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
    const char *fixture, *cc, *fixtures, *kind;
} config_t;

typedef struct {
    scenario_run_t *r;
    config_t *c;
    const char *root, *cc;
    unsigned serial;
} suite_t;

typedef struct {
    suite_t *s;
    loading_owner_t *transport;
    const char *root;
} owner_t;

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
    char *q = canonical_fs_path(p, &e);
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

static const char *read_text(scenario_run_t *r, const char *path, size_t *size) {
    size_t n;
    const char *raw = language_read(r, path, &n);
    REQUIRE(utf8((const unsigned char *)raw, n));
    REQUIRE(!memchr(raw, 0, n));
    char *text = language_owned(r, ALLOC(n + 1));
    size_t used = 0;
    for (size_t i = 0; i < n; i++) {
        if (raw[i] == '\r') {
            text[used++] = '\n';
            if (i + 1 < n && raw[i + 1] == '\n')
                i++;
        } else
            text[used++] = raw[i];
    }
    text[used] = 0;
    *size = used;
    return text;
}

static bool unicode_space(unsigned cp) {
    return (cp >= 9 && cp <= 13) || (cp >= 0x1c && cp <= 0x20) || cp == 0x85 || cp == 0xa0
           || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 || cp == 0x2029
           || cp == 0x202f || cp == 0x205f || cp == 0x3000;
}

static void strip(const char *text, size_t n, size_t *first, size_t *last) {
    *first = n;
    *last = 0;
    for (size_t i = 0; i < n;) {
        size_t at = i;
        unsigned c = (unsigned char)text[i++], cp = c, left = 0;
        if (c >= 0xf0) {
            cp = c & 7;
            left = 3;
        } else if (c >= 0xe0) {
            cp = c & 15;
            left = 2;
        } else if (c >= 0xc0) {
            cp = c & 31;
            left = 1;
        }
        while (left--)
            cp = (cp << 6) | ((unsigned char)text[i++] & 63);
        if (!unicode_space(cp)) {
            if (*first == n)
                *first = at;
            *last = i;
        }
    }
    if (*first == n)
        *last = n;
}

static bool inspect(scenario_run_t *r, const char *path, bool follow, fs_info_t *info) {
    fs_error_t e = {0};
    bool ok = inspect_fs_path(path, follow, info, &e), missing = e.status == FS_NOT_FOUND;
    clear_fs_error(&e);
    REQUIRE(ok || missing);
    return ok;
}

static bool exists(scenario_run_t *r, const char *path) {
    fs_info_t info;
    return inspect(r, path, true, &info);
}

static bool file(scenario_run_t *r, const char *path, bool nonempty) {
    fs_info_t info;
    return inspect(r, path, true, &info) && info.kind == FS_FILE && (!nonempty || info.size);
}

static bool clear_entries(void *p) {
    clear_fs_entries(p);
    FREE(p);
    return true;
}

static fs_entries_t *entries(scenario_run_t *r, const char *path) {
    fs_entries_t *out = CALLOC(sizeof(*out));
    scenario_defer(r, clear_entries, out);
    fs_error_t e = {0};
    bool ok = list_fs_directory(path, out, &e);
    clear_fs_error(&e);
    REQUIRE(ok);
    return out;
}

static bool separator(char c) {
    return c == '/'
#ifdef _WIN32
           || c == '\\'
#endif
        ;
}

/* pathlib removes redundant separators and '.' components, but not '..'.
 * Preserve a two-separator root (UNC/extended prefixes and POSIX //). */
static const char *pathlib_spelling(scenario_run_t *r, const char *path) {
    size_t n = strlen(path), used = 0, i = 0;
    char *out = language_owned(r, ALLOC(n + 1));
    if (separator(path[0])) {
        out[used++] = path[i++];
        if (separator(path[i]))
            out[used++] = path[i++];
    }
    while (i < n) {
        while (i < n && separator(path[i]))
            i++;
        size_t start = i;
        while (i < n && !separator(path[i]))
            i++;
        size_t length = i - start;
        if (!length || (length == 1 && path[start] == '.'))
            continue;
        if (used && !separator(out[used - 1]))
            out[used++] = '/';
        memcpy(out + used, path + start, length);
        used += length;
    }
#ifdef _WIN32
    for (size_t j = 0; j < used; j++)
        if (out[j] == '/')
            out[j] = '\\';
#endif
    out[used] = 0;
    return out;
}

static const char *parent(scenario_run_t *r, const char *path) {
    path = pathlib_spelling(r, path);
    const char *slash = NULL;
    for (const char *p = path; *p; p++)
        if (separator(*p))
            slash = p;
    REQUIRE(slash && slash > path);
    return string(r, "%.*s", (int)(slash - path), path);
}

static bool same_file(scenario_run_t *r, const char *a, const char *b) {
    fs_info_t x, y;
    REQUIRE(inspect(r, a, true, &x) && inspect(r, b, true, &y));
    return x.device == y.device && x.identity == y.identity;
}

static const char *put_fixture(suite_t *s, const char *target, const char *name) {
    scenario_run_t *r = s->r;
    size_t n;
    const char *data = language_read(r, language_join(r, s->c->fixtures, name), &n);
    language_write(r, target, data, n);
    return target;
}

static const char *const *
environment(scenario_run_t *r, const char *root, const char *const *extra) {
    const char *const *base = language_environment_override(r, "TMPDIR", root);
    const char *fixed[] = {string(r, "TMPDIR=%s", root),
                           string(r, "TMP=%s", root),
                           string(r, "TEMP=%s", root),
                           string(r, "GOAT_NATIVE_TEST_MARKERS=%s", root),
                           NULL};
    size_t n = 0, m = 0;
    while (base[n])
        n++;
    while (extra && extra[m])
        m++;
    const char **replace = language_owned(r, CALLOC((m + 5) * sizeof(*replace)));
    for (size_t i = 0; i < 4; i++)
        replace[i] = fixed[i];
    for (size_t i = 0; i < m; i++)
        replace[4 + i] = extra[i];
    const char **out = language_owned(r, CALLOC((n + m + 5) * sizeof(*out)));
    size_t used = 0;
    for (size_t i = 0; i < n; i++) {
        bool drop = false;
        for (size_t j = 0; j < m + 4; j++) {
            const char *eq = strchr(replace[j], '=');
            REQUIRE(eq);
            size_t len = (size_t)(eq - replace[j]);
#ifdef _WIN32
            drop |= !_strnicmp(base[i], replace[j], len) && base[i][len] == '=';
#else
            drop |= !strncmp(base[i], replace[j], len) && base[i][len] == '=';
#endif
        }
        if (!drop)
            out[used++] = base[i];
    }
    for (size_t i = 0; i < m + 4; i++)
        out[used++] = replace[i];
    return out;
}

static owner_t *start(suite_t *s, const char *const *tail, const char *const *extra) {
    scenario_run_t *r = s->r;
    owner_t *o = language_owned(r, CALLOC(sizeof(*o)));
    o->s = s;
    const char *name = string(r, "%03u", ++s->serial);
    o->root = language_join(r, s->root, string(r, "owner-%s", name));
    size_t n = 0;
    while (tail[n])
        n++;
    const char **args = language_owned(r, CALLOC((n + 2) * sizeof(*args)));
    args[0] = s->c->fixture;
    for (size_t i = 0; i < n; i++)
        args[i + 1] = tail[i];
    o->transport = loading_owner_command_start(r,
                                               s->root,
                                               name,
                                               args,
                                               environment(r, o->root, extra),
                                               NULL,
                                               0,
                                               false);
    return o;
}

static uint64_t deadline(scenario_run_t *r, uint64_t ms) {
    uint64_t now = child_monotonic_ms(), end = scenario_run_deadline(r),
             remaining = end > now ? end - now : 0;
    if (remaining > ms)
        remaining = ms;
    uint64_t watch = process_watch_now_ms();
    REQUIRE(watch != UINT64_MAX && remaining <= UINT64_MAX - watch);
    return watch + remaining;
}

static void wait_watch(owner_t *o, process_watch_t *w, uint64_t ms) {
    scenario_run_t *r = o->s->r;
    unsigned long e = 0;
    REQUIRE(process_watch_wait(w, deadline(r, ms), &e) == PROCESS_WATCH_OK);
}

static void alive(owner_t *o, process_watch_t *w) {
    scenario_run_t *r = o->s->r;
    unsigned long e = 0;
    bool live = false;
    REQUIRE(process_watch_alive(w, &live, &e) == PROCESS_WATCH_OK && live);
}

static void kill_watch(owner_t *o, process_watch_t *w) {
    scenario_run_t *r = o->s->r;
    unsigned long e = 0;
    REQUIRE(process_watch_kill(w, &e) == PROCESS_WATCH_OK);
}

static void line(owner_t *o, const char *expected) {
    scenario_run_t *r = o->s->r;
    loading_bytes_t bytes;
    REQUIRE(loading_owner_next_line(o->transport, 30000, &bytes));
    const char *p = bytes.data;
    size_t n = bytes.size;
    while (n && (p[n - 1] == '\r' || p[n - 1] == '\n'))
        n--;
    scenario_require_bytes(r, "owner line", expected, strlen(expected), p, n, false);
}

static void eof(owner_t *o, uint64_t ms) {
    scenario_run_t *r = o->s->r;
    loading_bytes_t bytes;
    REQUIRE(!loading_owner_next_line(o->transport, ms, &bytes));
}

static void resume(owner_t *o) {
    loading_owner_send(o->transport, "go\n", 3, 30000);
}

static loading_bytes_t end(owner_t *o, int status) {
    scenario_run_t *r = o->s->r;
    loading_owner_close_stdin(o->transport);
    loading_owner_reap(o->transport, 30000);
    const child_result_t *result = loading_owner_command_poll(o->transport);
    REQUIRE(result->status == CHILD_EXITED && result->exit_code == status);
    loading_owner_wait_stderr(o->transport, 30000);
    loading_bytes_t err = loading_owner_error(o->transport);
    const unsigned char *p = err.data;
    for (size_t i = 0; i + 4 <= err.size; i++)
        REQUIRE(!(tolower(p[i]) == 'l' && tolower(p[i + 1]) == 'e' && tolower(p[i + 2]) == 'a'
                  && tolower(p[i + 3]) == 'k'));
    return err;
}

static size_t count(loading_bytes_t bytes, const char *needle) {
    size_t n = strlen(needle), found = 0;
    for (size_t i = 0; n <= bytes.size && i <= bytes.size - n;) {
        if (!memcmp((const char *)bytes.data + i, needle, n)) {
            found++;
            i += n;
        } else
            i++;
    }
    return found;
}

typedef struct {
    owner_t *owner;
    const char *path, *value;
} marker_t;

static process_watch_result_t read_marker(void *p, unsigned long *error) {
    (void)error;
    marker_t *m = p;
    scenario_run_t *r = m->owner->s->r;
    if (!exists(r, m->path))
        return PROCESS_WATCH_TIMEOUT;
    size_t n;
    const char *v = read_text(r, m->path, &n);
    size_t first, last;
    strip(v, n, &first, &last);
    if (first == last)
        return PROCESS_WATCH_TIMEOUT;
    REQUIRE(!memchr(v, 0, n));
    m->value = string(r, "%.*s", (int)(last - first), v + first);
    return PROCESS_WATCH_OK;
}

static const char *marker(owner_t *o, const char *name) {
    scenario_run_t *r = o->s->r;
    marker_t m = {o, language_join(r, o->root, name), NULL};
    unsigned long e = 0;
    REQUIRE(process_watch_until(read_marker, &m, deadline(r, 30000), &e) == PROCESS_WATCH_OK);
    return m.value;
}

static process_watch_t *watch(owner_t *o, const char *name) {
    scenario_run_t *r = o->s->r;
    const char *v = marker(o, name);
    char *endptr;
    errno = 0;
    unsigned long long pid = strtoull(v, &endptr, 10);
    REQUIRE(!errno && pid && *v >= '0' && *v <= '9' && !*endptr);
    return loading_owner_watch_pid(o->transport, pid);
}

static size_t roots(owner_t *o, const char **first) {
    scenario_run_t *r = o->s->r;
    fs_entries_t *list = entries(r, o->root);
    size_t n = 0;
    for (size_t i = 0; i < list->count; i++)
        if (!strncmp(list->names[i], "goat-run-", 9)) {
            if (first)
                *first = language_join(r, o->root, list->names[i]);
            n++;
        }
    return n;
}

static bool artifacts(owner_t *o) {
    scenario_run_t *r = o->s->r;
    fs_entries_t *list = entries(r, o->root);
    for (size_t i = 0; i < list->count; i++)
        if (!strncmp(list->names[i], "goat-run-", 9)) {
            const char *root = language_join(r, o->root, list->names[i]);
            fs_entries_t *dirs = entries(r, root);
            for (size_t j = 0; j < dirs->count; j++)
                if (!strncmp(dirs->names[j], "native", 6)
                    && entries(r, language_join(r, root, dirs->names[j]))->count)
                    return true;
        }
    return false;
}

static size_t libraries(scenario_run_t *r, const char *directory, unsigned depth) {
    REQUIRE(depth < 128);
    fs_entries_t *list = entries(r, directory);
    size_t n = 0;
    for (size_t i = 0; i < list->count; i++) {
        const char *path = language_join(r, directory, list->names[i]);
        fs_info_t info;
        REQUIRE(inspect(r, path, false, &info));
        const char *suffix = strrchr(list->names[i], '.');
        if (suffix && suffix != list->names[i]
            && (!strcmp(suffix, ".dll") || !strcmp(suffix, ".so"))) {
            REQUIRE(file(r, path, false));
            n++;
        }
        if (info.kind == FS_DIRECTORY)
            n += libraries(r, path, depth + 1);
    }
    return n;
}

static void report(scenario_run_t *r, const char *path) {
    size_t n;
    const char *bytes = read_text(r, path, &n);
    REQUIRE(!memchr(bytes, 0, n));
    const char *p = bytes;
    bool found = false;
    long long succeeded = 0;
    while (*p) {
        const char *endline = strchr(p, '\n');
        if (!endline)
            endline = p + strlen(p);
        const char *eq = memchr(p, '=', (size_t)(endline - p));
        REQUIRE(eq);
        if (eq - p == 9 && !memcmp(p, "succeeded", 9)) {
            char *value = (char *)string(r, "%.*s", (int)(endline - eq - 1), eq + 1), *endptr;
            errno = 0;
            succeeded = strtoll(value, &endptr, 10);
            REQUIRE(endptr != value && !errno);
            while (isspace((unsigned char)*endptr))
                endptr++;
            REQUIRE(!*endptr);
            found = true;
        }
        p = *endline ? endline + 1 : endline;
    }
    REQUIRE(found && succeeded > 0);
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
    suite_t s = {.r = r, .c = c};
    s.root = language_join(r, scenario_work_directory(scenario_run_context(r)), "данные 😀");
    language_directory(r, s.root);
    s.cc = find_tool(r, c->cc);
    REQUIRE(s.cc);
    const char *k = c->kind,
               *program = put_fixture(&s, language_join(r, s.root, "program.goat"), "program.goat");
    bool explicit_exit = strlen(k) >= 5 && !strcmp(k + strlen(k) - 5, "-exit");
    const char *mode = strstr(k, "-auto") ? "auto" : "required";
    if (!strcmp(k, "empty-inventory")) {
        const char *source = put_fixture(&s, language_join(r, s.root, "empty.goat"), "empty.goat");
        owner_t *o = start(&s, ARGS("--native", "required", source), NULL);
        REQUIRE(end(o, 1).size);
        eof(o, 2000);
        REQUIRE(!exists(r, language_join(r, o->root, "cleaner.pid")) && !roots(o, NULL));
    } else if (!strncmp(k, "failure-", 8)) {
        const char *fault = strstr(k, "failure-exit-") ? "exit" : "silent";
        owner_t *o = start(&s,
                           ARGS("--native", mode, "--cc", c->fixture, program),
                           ARGS(string(r, "GOAT_NATIVE_TEST_CLEANER_FAULT=%s", fault)));
        if (!strcmp(mode, "auto")) {
            line(o, "READY");
            resume(o);
            line(o, "9");
        }
        loading_bytes_t err = end(o, !strcmp(mode, "auto") ? 0 : 1);
        REQUIRE(count(err, "Native preparation failed (cleanup-error)") == 1);
        REQUIRE(!exists(r, language_join(r, o->root, "compiler.query")) && !roots(o, NULL));
    } else if (!strncmp(k, "cleaner-death-", 14) || !strncmp(k, "normal-", 7)) {
        bool death = !strncmp(k, "cleaner-death-", 14);
        const char *source = put_fixture(&s,
                                         language_join(r, s.root, "code.goat"),
                                         explicit_exit ? "code-exit.goat" : "program.goat"),
                   *saved = language_join(r, s.root, "report.txt");
        owner_t *o =
            start(&s,
                  death ? ARGS("--native", mode, "--cc", s.cc, "--save-native", saved, source)
                        : ARGS("--native", "required", "--cc", s.cc, source),
                  NULL);
        line(o, "READY");
        process_watch_t *cleaner = watch(o, "cleaner.pid");
        if (death) {
            if (!artifacts(o)) {
                resume(o);
                loading_bytes_t error = end(o, explicit_exit ? 23 : 0);
                fprintf(stderr,
                        "native workspace missing: %s; stderr: %.*s\n",
                        o->root,
                        (int)error.size,
                        (const char *)error.data);
                if (exists(r, saved)) {
                    size_t n;
                    const char *text = read_text(r, saved, &n);
                    fprintf(stderr, "native report: %.*s\n", (int)n, text);
                }
                REQUIRE(false);
            }
            kill_watch(o, cleaner);
            wait_watch(o, cleaner, 10000);
        }
        resume(o);
        line(o, "9");
        REQUIRE(!end(o, explicit_exit ? 23 : 0).size);
        if (death) {
            if (explicit_exit)
                REQUIRE(!exists(r, saved));
            else
                report(r, saved);
        } else {
            wait_watch(o, cleaner, 10000);
            REQUIRE(!roots(o, NULL));
        }
    } else if (!strncmp(k, "module-", 7)) {
        const char *project = language_join(r, s.root, "module");
        language_directory(r, project);
        put_fixture(&s,
                    language_join(r, project, "main.goat"),
                    explicit_exit ? "module-exit.goat" : "module-normal.goat");
        const char *descriptor =
            put_fixture(&s, language_join(r, project, "module.gmod"), "module.gmod");
        owner_t *producer =
            start(&s, ARGS("build", descriptor, "--native", "required", "--cc", s.cc), NULL);
        REQUIRE(!end(producer, 0).size);
        REQUIRE(!strcmp(marker(producer, "cleaner.done"), "0") && !roots(producer, NULL));
        const char *archive = language_join(r, project, "modules/cleanup-module/1.gbin");
        REQUIRE(file(r, archive, false));
        size_t archive_size;
        const char *archive_before = language_read(r, archive, &archive_size);
        owner_t *o = start(&s,
                           ARGS("run",
                                strstr(k, "module-descriptor-") ? descriptor : archive,
                                "--native",
                                mode,
                                "--cc",
                                s.cc),
                           NULL);
        line(o, "9");
        line(o, "READY");
        process_watch_t *cleaner = watch(o, "cleaner.pid");
        const char *directory = NULL;
        REQUIRE(roots(o, &directory) == 1);
        alive(o, cleaner);
        REQUIRE(libraries(r, directory, 0) > 0);
        REQUIRE(loading_owner_command_poll(o->transport)->status == CHILD_RUNNING);
        resume(o);
        line(o, "10");
        REQUIRE(!end(o, explicit_exit ? 23 : 0).size);
        eof(o, 5000);
        wait_watch(o, cleaner, 40000);
        REQUIRE(!strcmp(marker(o, "cleaner.done"), "0"));
        REQUIRE(!exists(r, directory) && file(r, archive, false));
        size_t after_size;
        const char *archive_after = language_read(r, archive, &after_size);
        scenario_require_bytes(r,
                               "saved archive unchanged",
                               archive_before,
                               archive_size,
                               archive_after,
                               after_size,
                               false);
    } else if (!strcmp(k, "saved-library") || !strcmp(k, "compiled-artifacts")) {
        bool compiled = !strcmp(k, "compiled-artifacts");
        const char *source =
            put_fixture(&s, language_join(r, s.root, "export.goat"), "export.goat");
        owner_t *o =
            start(&s,
                  compiled ? ARGS("--cc", s.cc, "--compile", "--native", "required", source)
                           : ARGS("--cc", s.cc, "--save-library", source),
                  NULL);
        REQUIRE(!end(o, 0).size);
        REQUIRE(file(r, language_join(r, s.root, "export" EXT), true));
        if (compiled)
            REQUIRE(file(r, language_join(r, s.root, "export.gbin"), true));
        REQUIRE(!roots(o, NULL));
    } else {
        REQUIRE(!strcmp(k, "interrupted-build"));
        const char *unpublished = language_join(r, s.root, "never-published.so");
        owner_t *a = start(&s, ARGS("build", unpublished), ARGS("GOAT_NATIVE_TEST_BLOCK=1")),
                *b = start(&s,
                           ARGS("build", language_join(r, s.root, "other.so")),
                           ARGS("GOAT_NATIVE_TEST_BLOCK=1"));
        process_watch_t *ac = watch(a, "cleaner.pid"), *ap = watch(a, "compiler.pid"),
                        *aw = watch(a, "worker.pid"), *bc = watch(b, "cleaner.pid"),
                        *bp = watch(b, "compiler.pid"), *bw = watch(b, "worker.pid");
        const char *output = marker(a, "compiler.output"), *dir = parent(r, output),
                   *workspace = parent(r, dir);
        REQUIRE(same_file(r, parent(r, workspace), a->root));
        REQUIRE(file(r, pathlib_spelling(r, language_join(r, dir, "module.c")), false));
        alive(a, ac);
        alive(b, bc);
        loading_owner_terminate(a->transport, false, 10000);
        wait_watch(a, ap, 10000);
        wait_watch(a, aw, 10000);
        wait_watch(a, ac, 10000);
        REQUIRE(!exists(r, workspace) && !exists(r, unpublished));
        alive(b, bp);
        alive(b, bw);
        alive(b, bc);
        loading_owner_terminate(b->transport, false, 10000);
        wait_watch(b, bp, 10000);
        wait_watch(b, bw, 10000);
        wait_watch(b, bc, 10000);
    }
}

static const char *kinds[] = {"empty-inventory",
                              "failure-exit-auto",
                              "failure-exit-required",
                              "failure-silent-auto",
                              "failure-silent-required",
                              "cleaner-death-auto-normal",
                              "cleaner-death-auto-exit",
                              "cleaner-death-required-normal",
                              "cleaner-death-required-exit",
                              "normal-normal",
                              "normal-exit",
                              "module-descriptor-auto-normal",
                              "module-descriptor-auto-exit",
                              "module-descriptor-required-normal",
                              "module-descriptor-required-exit",
                              "module-archive-auto-normal",
                              "module-archive-auto-exit",
                              "module-archive-required-normal",
                              "module-archive-required-exit",
                              "saved-library",
                              "compiled-artifacts",
                              "interrupted-build"};

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
    int result = 1;
    if (argc == 7 || (argc == 8 && scenario_keep_work_argument(argv[7]))) {
        enum { COUNT = sizeof(kinds) / sizeof(kinds[0]) };

        config_t configs[COUNT];
        scenario_definition_t definitions[COUNT];
        char ids[COUNT][120];
        fs_error_t e = {0};
        char *fixture = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        if (fixture) {
            for (size_t i = 0; i < COUNT; i++) {
                snprintf(ids[i], sizeof(ids[i]), "integration.native_cleanup.%s", kinds[i]);
                configs[i] = (config_t){fixture, argv[3], argv[6], kinds[i]};
                definitions[i] = (scenario_definition_t){ids[i], 480000, body, configs + i, NULL};
            }
            scenario_options_t options = {.build_directory = argv[4], .data_directory = argv[5]};
            const char *selection[] = {argv[1], argc == 8 ? argv[7] : NULL};
            result =
                scenario_registry_main(definitions, COUNT, argc == 8 ? 2 : 1, selection, &options);
        }
        FREE(fixture);
    } else
        fputs("usage: native_cleanup_scenarios ID FIXTURE CC BUILD DATA FIXTURES [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
