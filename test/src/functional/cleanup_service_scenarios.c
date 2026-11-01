/** @file cleanup_service_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent owner/cleaner acceptance scenarios (#376).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/module_loading_owner.h"
#include "test/support/registry.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include "lib/platform/windows_target.h"

#    include <windows.h>
#endif
#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})

typedef struct {
    const char *fixture, *kind;
    bool skipped;
} config_t;

typedef struct {
    scenario_run_t *r;
    config_t *c;
    const char *root;
    unsigned serial;
} suite_t;

typedef struct {
    suite_t *s;
    loading_owner_t *transport;
    const char *root, *directory, *pid, *done;
    process_watch_t *cleaner, *workers[2];
} owner_t;

static bool exists(scenario_run_t *r, const char *path) {
    fs_error_t e = {0};
    fs_info_t info;
    bool ok = inspect_fs_path(path, true, &info, &e), missing = e.status == FS_NOT_FOUND;
    clear_fs_error(&e);
    REQUIRE(ok || missing);
    return ok;
}

static uint64_t deadline(scenario_run_t *r, uint64_t ms) {
    uint64_t end = scenario_run_deadline(r), now = child_monotonic_ms();
    uint64_t remaining = end > now ? end - now : 0;
    if (remaining > ms)
        remaining = ms;
    return process_watch_now_ms() + remaining;
}

static void wait_watch(owner_t *o, process_watch_t *w, uint64_t ms) {
    scenario_run_t *r = o->s->r;
    unsigned long error = 0;
    REQUIRE(process_watch_wait(w, deadline(r, ms), &error) == PROCESS_WATCH_OK);
}

static const char *line(owner_t *o, uint64_t ms) {
    scenario_run_t *r = o->s->r;
    loading_bytes_t bytes;
    REQUIRE(loading_owner_next_line(o->transport, ms, &bytes));
    const char *p = bytes.data;
    size_t n = bytes.size;
    while (n && (p[n - 1] == '\r' || p[n - 1] == '\n'))
        n--;
    REQUIRE(!memchr(p, 0, n));
    char *out = language_owned(r, ALLOC(n + 1));
    memcpy(out, p, n);
    out[n] = 0;
    return out;
}

static void expect(owner_t *o, const char *expected, uint64_t ms) {
    scenario_run_t *r = o->s->r;
    const char *actual = line(o, ms);
    scenario_require_bytes(r,
                           "owner protocol line",
                           expected,
                           strlen(expected),
                           actual,
                           strlen(actual),
                           false);
}

static void send_command(owner_t *o, const char *command) {
    loading_owner_send(o->transport, command, strlen(command), 10000);
}

static bool env_key(const char *entry, const char *key) {
    size_t n = strlen(key);
#ifdef _WIN32
    return !_strnicmp(entry, key, n) && entry[n] == '=';
#else
    return !strncmp(entry, key, n) && entry[n] == '=';
#endif
}

static owner_t *start(suite_t *s, const char *mode, const char *fault) {
    scenario_run_t *r = s->r;
    owner_t *o = language_owned(r, ALLOC(sizeof(*o)));
    memset(o, 0, sizeof(*o));
    o->s = s;
    char name[40], leaf[48];
    snprintf(name, sizeof(name), "%03u", ++s->serial);
    snprintf(leaf, sizeof(leaf), "owner-%s", name);
    o->root = language_join(r, s->root, leaf);
    o->pid = language_join(r, o->root, "cleaner.pid");
    o->done = language_join(r, o->root, "cleaner.done");
    const char *keys[] = {"TMPDIR",
                          "TMP",
                          "TEMP",
                          "GOAT_TEST_CLEANER_PID",
                          "GOAT_TEST_CLEANER_DONE",
                          "GOAT_TEST_CLEANER_FAULT"};
    const char *values[] = {o->root, o->root, o->root, o->pid, o->done, fault};
    const char *const *base = language_environment_override(r, "TMPDIR", o->root);
    size_t n = 0;
    while (base[n])
        n++;
    const char **env = language_owned(r, ALLOC((n + 7) * sizeof(*env)));
    size_t used = 0;
    for (size_t i = 0; i < n; i++) {
        bool drop = false;
        for (size_t j = 0; j < 6; j++)
            drop |= env_key(base[i], keys[j]);
        if (!drop)
            env[used++] = base[i];
    }
    for (size_t j = 0; j < 6; j++)
        if (values[j]) {
            size_t len = strlen(keys[j]) + strlen(values[j]) + 2;
            char *v = language_owned(r, ALLOC(len));
            snprintf(v, len, "%s=%s", keys[j], values[j]);
            env[used++] = v;
        }
    env[used] = NULL;
    o->transport = loading_owner_command_start(r,
                                               s->root,
                                               name,
                                               ARGS(s->c->fixture, mode),
                                               env,
                                               NULL,
                                               0,
                                               false);
    return o;
}

typedef struct {
    owner_t *owner;
    const char *path;
    uint64_t pid;
} pid_read_t;

static process_watch_result_t read_pid(void *arg, unsigned long *error) {
    (void)error;
    pid_read_t *p = arg;
    scenario_run_t *r = p->owner->s->r;
    if (!exists(r, p->path))
        return PROCESS_WATCH_TIMEOUT;
    size_t n;
    const char *text = language_read(r, p->path, &n);
    if (!n)
        return PROCESS_WATCH_TIMEOUT;
    REQUIRE(strlen(text) == n);
    char *end;
    errno = 0;
    unsigned long long pid = strtoull(text, &end, 10);
    REQUIRE(errno != ERANGE);
    while (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t')
        end++;
    REQUIRE(pid && !*end && *text >= '0' && *text <= '9');
    p->pid = pid;
    return PROCESS_WATCH_OK;
}

static process_watch_t *pin(owner_t *o, const char *path) {
    scenario_run_t *r = o->s->r;
    pid_read_t p = {o, path, 0};
    unsigned long error = 0;
    REQUIRE(process_watch_until(read_pid, &p, deadline(r, 10000), &error) == PROCESS_WATCH_OK);
    return loading_owner_watch_pid(o->transport, p.pid);
}

static size_t run_count(owner_t *o) {
    scenario_run_t *r = o->s->r;
    fs_entries_t entries = {0};
    fs_error_t e = {0};
    bool ok = list_fs_directory(o->root, &entries, &e);
    clear_fs_error(&e);
    REQUIRE(ok);
    size_t count = 0;
    for (size_t i = 0; i < entries.count; i++)
        if (!strncmp(entries.names[i], "goat-run-", 9))
            count++;
    clear_fs_entries(&entries);
    return count;
}

/* Python Path equality ignores repeated/trailing separators; on Windows it also
 * folds slash direction and case. Do not resolve symlinks or accept descendants. */
static char *comparison_path(const char *path) {
    char *out = malloc(strlen(path) + 1);
    if (!out)
        return NULL;
    size_t used = 0;
    for (size_t i = 0; path[i]; i++) {
        unsigned char c = (unsigned char)path[i];
#ifdef _WIN32
        if (c == '\\')
            c = '/';
        c = (unsigned char)tolower(c);
#endif
        if (c == '/' && used && out[used - 1] == '/')
            continue;
        out[used++] = (char)c;
    }
    while (used > 1 && out[used - 1] == '/')
        used--;
    out[used] = 0;
    return out;
}

static bool direct_child(const char *root, const char *path) {
    char *a = comparison_path(root), *b = comparison_path(path);
    bool valid = false;
    if (a && b) {
        size_t n = strlen(a);
        if (strlen(b) > n + 1 && !memcmp(a, b, n) && b[n] == '/') {
            const char *leaf = b + n + 1;
            valid = !strchr(leaf, '/') && strcmp(leaf, ".") && strcmp(leaf, "..");
        }
    }
    free(a);
    free(b);
    return valid;
}

static void ready(owner_t *o) {
    scenario_run_t *r = o->s->r;
    const char *v = line(o, 10000);
    REQUIRE(!strncmp(v, "ready ", 6));
    o->directory = v + 6;
    REQUIRE(direct_child(o->root, o->directory));
    REQUIRE(exists(r, language_join(r, o->directory, "данные 😀.tmp")));
    o->cleaner = pin(o, o->pid);
    REQUIRE(run_count(o) == 1);
}

static void ended(owner_t *o, int code) {
    scenario_run_t *r = o->s->r;
    loading_owner_reap(o->transport, 10000);
    const child_result_t *result = loading_owner_command_poll(o->transport);
    REQUIRE(result->status == CHILD_EXITED && result->exit_code == code);
}

static void done(owner_t *o, const char *value) {
    scenario_run_t *r = o->s->r;
    size_t n;
    const char *v = language_read(r, o->done, &n);
    while (n && (v[n - 1] == '\n' || v[n - 1] == '\r' || v[n - 1] == ' ' || v[n - 1] == '\t'))
        n--;
    REQUIRE(n == strlen(value) && !memcmp(v, value, n));
}

static void cleaned(owner_t *o) {
    scenario_run_t *r = o->s->r;
    wait_watch(o, o->cleaner, 10000);
    REQUIRE(!exists(r, o->directory));
    done(o, "0");
}

static owner_t *normal(suite_t *s) {
    owner_t *o = start(s, "owner", NULL);
    ready(o);
    return o;
}

static void workers(owner_t *o) {
    scenario_run_t *r = o->s->r;
    send_command(o, "run\n");
    o->workers[0] = pin(o, language_join(r, o->directory, "compiler"));
    o->workers[1] = pin(o, language_join(r, o->directory, "worker"));
}

static void alive(owner_t *o) {
    scenario_run_t *r = o->s->r;
    REQUIRE(exists(r, o->directory));
    for (size_t i = 0; i < 2; i++)
        loading_owner_watch_alive(o->transport, o->workers[i]);
}

static void stop(owner_t *o) {
    loading_owner_terminate(o->transport, false, 10000);
}

static void stop_workers(owner_t *o) {
    stop(o);
    for (size_t i = 0; i < 2; i++)
        wait_watch(o, o->workers[i], 10000);
    cleaned(o);
}

static void same_file(scenario_run_t *r, const char *path, const char *expected) {
    size_t n;
    const char *v = language_read(r, path, &n);
    scenario_require_bytes(r, "preserved bytes", expected, strlen(expected), v, n, false);
}
#ifdef _WIN32
static bool close_busy(void *p) {
    HANDLE *h = p;
    if (*h != INVALID_HANDLE_VALUE) {
        bool ok = CloseHandle(*h) != 0;
        *h = INVALID_HANDLE_VALUE;
        return ok;
    }
    return true;
}

static HANDLE *busy_file(owner_t *o) {
    scenario_run_t *r = o->s->r;
    const char *path = language_join(r, o->directory, "данные 😀.tmp");
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    REQUIRE(n > 0);
    wchar_t *wide = language_owned(r, ALLOC((size_t)n * sizeof(*wide)));
    REQUIRE(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, n) == n);
    HANDLE *h = language_owned(r, ALLOC(sizeof(*h)));
    *h = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    REQUIRE(*h != INVALID_HANDLE_VALUE);
    scenario_defer(r, close_busy, h);
    return h;
}
#endif
#ifndef _WIN32
static bool remove_native(void *p) {
    return !remove(p) || errno == ENOENT;
}
#endif
static void action(suite_t *s, const char *kind) {
    scenario_run_t *r = s->r;
    owner_t *o = normal(s);
    if (!strcmp(kind, "normal")) {
        const char *old = language_join(r, o->root, "goat-run-old"),
                   *saved = language_join(r, o->root, "saved.so");
        language_directory(r, old);
        const char *gbin = language_join(r, old, "saved.gbin");
        language_write(r, gbin, "saved", 5);
        language_write(r, saved, "published", 9);
        send_command(o, "short\n");
        expect(o, "ran 0 1", 10000);
        REQUIRE(exists(r, language_join(r, o->directory, "relative-output")));
        send_command(o, "finish\n");
        expect(o, "finished 0", 10000);
        cleaned(o);
        same_file(r, saved, "published");
        same_file(r, gbin, "saved");
        send_command(o, "quit\n");
        ended(o, 0);
    } else if (!strcmp(kind, "owner-quit") || !strcmp(kind, "owner-exit")) {
        bool exit = !strcmp(kind, "owner-exit");
        send_command(o, exit ? "exit\n" : "quit\n");
        ended(o, exit ? 23 : 0);
        cleaned(o);
    } else if (!strcmp(kind, "detached-client")) {
        send_command(o, "detach\n");
        expect(o, "detached", 10000);
        REQUIRE(exists(r, o->directory) && !exists(r, o->done));
        send_command(o, "touch\n");
        expect(o, "still-owner", 10000);
        same_file(r, language_join(r, o->directory, "данные 😀.tmp"), "43\n");
        send_command(o, "quit\n");
        ended(o, 0);
        cleaned(o);
    } else if (!strcmp(kind, "invalid-native-name")) {
#ifndef _WIN32
        size_t length = strlen(o->directory) + 12;
        char *bad = language_owned(r, ALLOC(length));
        snprintf(bad, length, "%s/invalid-\xff", o->directory);
        FILE *f = fopen(bad, "wb");
        REQUIRE(f);
        scenario_defer(r, remove_native, (void *)bad);
        bool written = fwrite("not UTF-8", 1, 9, f) == 9;
        int closed = fclose(f);
        REQUIRE(written && !closed);
        send_command(o, "finish\n");
        expect(o, "finished 4", 10000);
        wait_watch(o, o->cleaner, 10000);
        REQUIRE(exists(r, o->directory));
        done(o, "1");
        send_command(o, "quit\n");
        ended(o, 0);
        /* Native invalid bytes cannot pass the UTF-8 scenario cleanup API. */
        REQUIRE(!remove(bad));
#endif
    } else if (!strcmp(kind, "killed-owner")) {
        stop(o);
        cleaned(o);
    } else if (!strncmp(kind, "dead-cleaner-", 13)) {
        loading_owner_watch_kill(o->transport, o->cleaner);
        wait_watch(o, o->cleaner, 10000);
        send_command(o, "check\n");
        expect(o, "alive", 10000);
        send_command(o, "short\n");
        expect(o, "ran 3 -1", 10000);
        bool exit = !strcmp(kind, "dead-cleaner-23");
        send_command(o, exit ? "exit\n" : "quit\n");
        ended(o, exit ? 23 : 0);
        REQUIRE(exists(r, o->directory));
    } else if (!strcmp(kind, "active-compiler")) {
        workers(o);
        stop_workers(o);
    } else if (!strcmp(kind, "child-deadline")) {
        send_command(o, "timeout\n");
        expect(o, "ran 0 3", 10000);
        send_command(o, "finish\n");
        expect(o, "finished 0", 10000);
        cleaned(o);
        send_command(o, "quit\n");
        ended(o, 0);
    } else if (!strncmp(kind, "cleanup-race-", 13) || !strncmp(kind, "launch-race-", 12)) {
        send_command(o, !strncmp(kind, "cleanup-", 8) ? "finish\n" : "run\n");
        stop(o);
        cleaned(o);
    } else {
#ifdef _WIN32
        bool persistent = !strcmp(kind, "permanently-busy");
        HANDLE *h = busy_file(o);
        send_command(o, "finish\n");
        expect(o, "finished 2", 8000);
        REQUIRE(exists(r, o->directory));
        send_command(o, "quit\n");
        ended(o, 0);
        if (persistent) {
            wait_watch(o, o->cleaner, 28000);
            REQUIRE(exists(r, o->directory));
            done(o, "1");
        }
        REQUIRE(close_busy(h));
        if (!persistent)
            cleaned(o);
#else
        REQUIRE(false);
#endif
    }
}

static void body(scenario_run_t *r, void *arg) {
    config_t *c = arg;
    suite_t s = {r, c, scenario_work_directory(scenario_run_context(r)), 0};
    const char *kind = c->kind;
    s.root = language_join(r, s.root, "cleanup-данные 😀");
    language_directory(r, s.root);
#ifdef _WIN32
    bool skip = !strcmp(kind, "invalid-native-name");
#else
    bool skip = !strcmp(kind, "busy") || !strcmp(kind, "permanently-busy");
#endif
    if (skip) {
        c->skipped = true;
        puts("SKIP: platform-specific native filename or Windows file sharing contract");
        return;
    }
    if (!strcmp(kind, "channel") || !strcmp(kind, "retry") || !strcmp(kind, "lazy")) {
        owner_t *o = start(&s, kind, NULL);
        if (!strcmp(kind, "lazy"))
            expect(o, "lazy", 10000);
        ended(o, 0);
        if (!strcmp(kind, "lazy"))
            REQUIRE(!exists(r, o->pid) && run_count(o) == 0);
    } else if (!strncmp(kind, "startup-", 8)) {
        const char *fault = kind + 8;
        bool missing = !strcmp(fault, "missing");
        owner_t *o = start(&s, missing ? "missing" : "failure", missing ? NULL : fault);
        REQUIRE(!strncmp(line(o, 4000), "failed ", 7));
        REQUIRE(run_count(o) == 0);
        if (!strcmp(fault, "silent") || !strcmp(fault, "late"))
            o->cleaner = pin(o, o->pid);
        send_command(o, "ack\n");
        ended(o, 0);
        if (o->cleaner)
            wait_watch(o, o->cleaner, 10000);
    } else if (!strcmp(kind, "parallel-lifetime")) {
        owner_t *b = normal(&s);
        workers(b);
        const char *sequence[] = {"owner-quit",
                                  "owner-exit",
                                  "detached-client",
#ifndef _WIN32
                                  "invalid-native-name",
#endif
                                  "killed-owner",
                                  "dead-cleaner-0",
                                  "dead-cleaner-23",
                                  "active-compiler",
                                  "child-deadline"};
        for (size_t i = 0; i < sizeof(sequence) / sizeof(sequence[0]); i++) {
            action(&s, sequence[i]);
            alive(b);
        }
        for (unsigned i = 0; i < 8; i++) {
            char k[40];
            snprintf(k, sizeof(k), "cleanup-race-%02u", i);
            action(&s, k);
            alive(b);
        }
        for (unsigned i = 0; i < 8; i++) {
            char k[40];
            snprintf(k, sizeof(k), "launch-race-%02u", i);
            action(&s, k);
            alive(b);
        }
#ifdef _WIN32
        action(&s, "busy");
        alive(b);
        action(&s, "permanently-busy");
        alive(b);
#endif
        alive(b);
        stop_workers(b);
    } else
        action(&s, kind);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t e = {0};
    if (!fs_arguments(&argc, &argv, &e)) {
        clear_fs_error(&e);
        return 1;
    }
#endif
    int result = 1;
    if (argc == 5 || (argc == 6 && !strcmp(argv[5], "--keep-work"))) {
        char *paths[5] = {0};
        bool valid = true;
        for (size_t i = 2; i < 5; i++) {
            fs_error_t e = {0};
            paths[i] = absolute_fs_path(argv[i], &e);
            clear_fs_error(&e);
            valid &= paths[i] != NULL;
        }
        const char *names[] = {"channel",
                               "retry",
                               "lazy",
                               "startup-missing",
                               "startup-exit",
                               "startup-silent",
                               "startup-late",
                               "normal",
                               "owner-quit",
                               "owner-exit",
                               "detached-client",
                               "invalid-native-name",
                               "killed-owner",
                               "dead-cleaner-0",
                               "dead-cleaner-23",
                               "active-compiler",
                               "child-deadline",
                               "cleanup-race-00",
                               "cleanup-race-01",
                               "cleanup-race-02",
                               "cleanup-race-03",
                               "cleanup-race-04",
                               "cleanup-race-05",
                               "cleanup-race-06",
                               "cleanup-race-07",
                               "launch-race-00",
                               "launch-race-01",
                               "launch-race-02",
                               "launch-race-03",
                               "launch-race-04",
                               "launch-race-05",
                               "launch-race-06",
                               "launch-race-07",
                               "busy",
                               "permanently-busy",
                               "parallel-lifetime"};

        enum { COUNT = sizeof(names) / sizeof(names[0]) };

        config_t configs[COUNT];
        scenario_definition_t defs[COUNT];
        char ids[COUNT][128];
        for (size_t i = 0; i < COUNT; i++) {
            snprintf(ids[i], sizeof(ids[i]), "integration.cleanup_service.%s", names[i]);
            configs[i] = (config_t){paths[2], names[i], false};
            defs[i] =
                (scenario_definition_t){ids[i],
                                        !strcmp(names[i], "parallel-lifetime") ? 180000 : 60000,
                                        body,
                                        configs + i,
                                        NULL};
        }
        scenario_options_t options = {.build_directory = paths[3], .data_directory = paths[4]};
        const char *selection[] = {argv[1], argc == 6 ? "--keep-work" : NULL};
        if (valid)
            result = scenario_registry_main(defs, COUNT, argc == 6 ? 2 : 1, selection, &options);
        if (!result)
            for (size_t i = 0; i < COUNT; i++)
                if (configs[i].skipped)
                    result = 77;
        for (size_t i = 2; i < 5; i++)
            FREE(paths[i]);
    } else
        fputs("usage: cleanup_service_scenarios ID FIXTURE BUILD DATA [--keep-work]\n", stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
