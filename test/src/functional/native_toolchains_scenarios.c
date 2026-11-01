/** @file native_toolchains_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent real compiler discovery observations (#379).
 */
#ifndef _WIN32
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#    define SUFFIX ".exe"
#    define SEP ';'
#else
#    include <sys/stat.h>
#    include <unistd.h>
#    define SUFFIX ""
#    define SEP ':'
#endif
#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})

typedef struct {
    const char *exe, *cc, *target, *kind;
    bool skipped;
} config_t;

typedef struct {
    scenario_run_t *r;
    config_t *c;
    const char *root, *wrapper, *log, *cc;
    const char *const *base;
} suite_t;

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

static const char *parent(scenario_run_t *r, const char *path) {
    const char *leaf = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            leaf = p;
    REQUIRE(leaf > path);
    return string(r, "%.*s", (int)(leaf - path), path);
}

static void copy(scenario_run_t *r, const char *from, const char *to) {
    size_t n;
    const char *bytes = language_read(r, from, &n);
    language_write(r, to, bytes, n);
#ifndef _WIN32
    REQUIRE(chmod(to, 0755) == 0);
#endif
}

static const char *const *
environment(scenario_run_t *r, const char *const *base, const char *const *extra) {
    size_t n = 0, m = 0, used = 0;
    while (base[n])
        n++;
    while (extra && extra[m])
        m++;
    const char **out = language_owned(r, CALLOC((n + m + 1) * sizeof(*out)));
    for (size_t i = 0; i < n; i++) {
        bool replace = false;
        for (size_t j = 0; j < m; j++) {
            const char *eq = strchr(extra[j], '=');
            REQUIRE(eq);
            size_t len = (size_t)(eq - extra[j]);
#ifdef _WIN32
            replace |= !_strnicmp(base[i], extra[j], len) && base[i][len] == '=';
#else
            replace |= !strncmp(base[i], extra[j], len) && base[i][len] == '=';
#endif
        }
        if (!replace)
            out[used++] = base[i];
    }
    for (size_t i = 0; i < m; i++)
        out[used++] = extra[i];
    return out;
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
    command_t *c = user;
    c->result = scenario_command_capture(r, &c->options);
    REQUIRE(c->result.exit_code == 0);
    REQUIRE(utf8(c->result.out, c->result.out_size));
    REQUIRE(utf8(c->result.err, c->result.err_size));
}

static const char *invoke(suite_t *s,
                          const char *cc,
                          const char *target,
                          const char *timeout,
                          const char *repetitions,
                          const char *mode,
                          const char *const *extra) {
    scenario_run_t *r = s->r;
    const char *args[] = {s->c->exe,
                          mode ? mode : "select",
                          cc ? cc : s->wrapper,
                          target ? target : s->c->target,
                          timeout ? timeout : "10",
                          repetitions ? repetitions : "1",
                          NULL};
    command_t c = {.options = {.executable = args[0],
                               .arguments = args,
                               .environment = environment(r, s->base, extra),
                               .empty_stdin = true}};
    scenario_with_timeout(r, 120000, command, &c);
    char *text = language_owned(r, ALLOC(c.result.out_size + 1));
    memcpy(text, c.result.out, c.result.out_size);
    text[c.result.out_size] = 0;
    REQUIRE(strlen(text) == c.result.out_size);
    return text;
}

static size_t occurrences(const char *text, const char *needle) {
    size_t n = 0;
    while ((text = strstr(text, needle))) {
        n++;
        text += strlen(needle);
    }
    return n;
}

/* Match Python str.splitlines(), including Unicode boundaries and a final
 * unterminated line. Repeated tokens in one line count only once. */
static size_t line_break(const char *p) {
    unsigned char c = (unsigned char)*p;
    if (c == '\r')
        return p[1] == '\n' ? 2 : 1;
    if (c == '\n' || c == '\v' || c == '\f' || c == 0x1c || c == 0x1d || c == 0x1e)
        return 1;
    if (c == 0xc2 && (unsigned char)p[1] == 0x85)
        return 2;
    if (c == 0xe2 && (unsigned char)p[1] == 0x80
        && ((unsigned char)p[2] == 0xa8 || (unsigned char)p[2] == 0xa9))
        return 3;
    return 0;
}

static size_t matching_lines(const char *text, const char *needle) {
    size_t count = 0;
    while (*text) {
        const char *end = text;
        while (*end && !line_break(end))
            end++;
        const char *match = needle ? strstr(text, needle) : text;
        if (match && match + (needle ? strlen(needle) : 0) <= end)
            count++;
        text = *end ? end + line_break(end) : end;
    }
    return count;
}

static const char *read_log(suite_t *s) {
    size_t n;
    const char *p = language_read(s->r, s->log, &n);
    scenario_require(s->r, utf8((const unsigned char *)p, n), "invalid UTF-8 log");
    scenario_require(s->r, !memchr(p, 0, n), "embedded NUL in compiler log");
    return string(s->r, "%.*s", (int)n, p);
}

static const char *host(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *out = invoke(s, NULL, NULL, NULL, NULL, NULL, NULL);
    const char *available = strstr(out, " available=1");
    REQUIRE(available);
    const char *start = available;
    while (start > out && start[-1] != '\n')
        start--;
    const char *target = strstr(start, " target=");
    REQUIRE(target && target < available);
    target += 8;
    return string(r, "%.*s", (int)(available - target), target);
}

static void selected(suite_t *s, const char *out, const char *name) {
    scenario_run_t *r = s->r;
    const char *p = out;
    while (*p) {
        const char *end = strchr(p, '\n');
        if (!end)
            end = p + strlen(p);
        const char *q = !strncmp(p, "round=", 6) ? strstr(p, " selected=") : NULL;
        if (q && q < end) {
            q += 10;
            while (end > q && end[-1] == '\r')
                end--;
            REQUIRE(strncmp(q, "none", (size_t)(end - q)));
            REQUIRE(!strcmp(canonical(r, string(r, "%.*s", (int)(end - q), q)),
                            canonical(r, language_join(r, s->root, name))));
            return;
        }
        p = *end ? end + 1 : end;
    }
    REQUIRE(false);
}

static bool ends(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && !strcmp(s + n - m, suffix);
}

static const char *const *isolation(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *names[] = {"cc" SUFFIX, "gcc" SUFFIX, "clang" SUFFIX};
    for (size_t i = 0; i < 3; i++)
        copy(r, s->c->exe, language_join(r, s->root, names[i]));
    const char *ld = find_tool(r, "ld");
#ifndef _WIN32
    const char *as = find_tool(r, "as");
    if (ld)
        REQUIRE(symlink(ld, language_join(r, s->root, "ld")) == 0);
    if (as)
        REQUIRE(symlink(as, language_join(r, s->root, "as")) == 0);
    const char *path = s->root;
#else
    char *raw = fs_environment("PATH", NULL);
    const char *old = raw ? language_owned(r, raw) : "";
    const char *path = string(r, "%s%c%s", s->root, SEP, old);
#endif
    const char *tools = parent(r, s->cc);
    if (ld)
        tools = string(r, "%s%c%s", tools, SEP, parent(r, ld));
    return environment(r,
                       s->base,
                       ARGS(string(r, "PATH=%s", path), string(r, "COMPILER_PATH=%s", tools)));
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
    const char *root = scenario_work_directory(scenario_run_context(r));
    suite_t s = {.r = r, .c = c, .root = root};
    s.cc = find_tool(r, c->cc);
    REQUIRE(s.cc);
    s.wrapper = language_join(r, root, "controlled-compiler" SUFFIX);
    s.log = language_join(r, root, "calls.log");
    copy(r, c->exe, s.wrapper);
    s.base = environment(r,
                         language_environment_override(r, "GOAT_TOOLCHAIN_REAL", s.cc),
                         ARGS(string(r, "GOAT_TOOLCHAIN_LOG=%s", s.log)));
    const char *k = c->kind, *out;
#define RUN(...) invoke(&s, __VA_ARGS__)
#define SIMPLE() RUN(NULL, NULL, NULL, NULL, NULL, NULL)
    if (!strcmp(k, "cache")) {
        out = RUN(NULL, NULL, NULL, "2", NULL, NULL);
        REQUIRE(occurrences(out, "available=1") == 2);
        REQUIRE(matching_lines(read_log(&s), " -std=c11") == 2);
    } else if (!strcmp(k, "reprobe")) {
        RUN(NULL, NULL, NULL, "2", NULL, NULL);
        size_t before = matching_lines(read_log(&s), NULL);
        SIMPLE();
        REQUIRE(matching_lines(read_log(&s), NULL) > before);
    } else if (!strcmp(k, "linux-relro") || !strcmp(k, "foreign-libc")
               || !strcmp(k, "musl-trace")) {
        const char *target = host(&s);
        if ((!strcmp(k, "musl-trace") && !ends(target, "-musl"))
            || (strcmp(k, "musl-trace") && strncmp(target, "linux-", 6))) {
            c->skipped = true;
            printf("SKIP: original target condition does not apply: %s\n", target);
            return;
        }
        if (!strcmp(k, "linux-relro")) {
            const char *log = language_join(r, root, "arguments.log");
            RUN(NULL,
                NULL,
                NULL,
                NULL,
                NULL,
                ARGS(string(r, "GOAT_TOOLCHAIN_ARGUMENTS_LOG=%s", log)));
            size_t n;
            const char *bytes = language_read(r, log, &n);
            const char expected[] = "-Wl,-z,relro,-z,now";
            bool found = false;
            for (size_t i = 0; i + sizeof(expected) <= n; i++)
                found |= !memcmp(bytes + i, expected, sizeof(expected));
            REQUIRE(found);
        } else if (!strcmp(k, "foreign-libc")) {
            const char *dash = strrchr(target, '-');
            REQUIRE(dash);
            out = RUN(NULL,
                      string(r,
                             "%.*s-%s",
                             (int)(dash - target),
                             target,
                             ends(target, "-musl") ? "gnu" : "musl"),
                      NULL,
                      NULL,
                      NULL,
                      NULL);
            REQUIRE(strstr(out, "available=0") && strstr(out, "selected=none"));
        } else {
            out = RUN(NULL, NULL, NULL, NULL, NULL, ARGS("GOAT_TOOLCHAIN_MODE=hide-trace"));
            REQUIRE(strstr(out, "available=0")
                    && strstr(out, "linked musl runtime cannot be established"));
        }
    } else if (!strcmp(k, "unicode-temp")) {
        const char *temp = language_join(r, root, "временные файлы 漢字");
        language_directory(r, temp);
        out = RUN(NULL,
                  NULL,
                  NULL,
                  NULL,
                  NULL,
                  ARGS(string(r, "TMPDIR=%s", temp),
                       string(r, "TMP=%s", temp),
                       string(r, "TEMP=%s", temp)));
        REQUIRE(strstr(out, "available=1"));
    } else if (!strcmp(k, "timeout-cache")) {
        out = RUN(NULL, NULL, NULL, "2", "vary-timeout", NULL);
        REQUIRE(matching_lines(read_log(&s), " -std=c11") == 4);
        REQUIRE(strstr(out, "name=GCC version=") || strstr(out, "name=Clang version="));
    } else if (!strcmp(k, "unsupported-target") || !strcmp(k, "crypto") || !strcmp(k, "musl-crypto")
               || !strcmp(k, "unknown-profile")) {
        const char *target = !strcmp(k, "crypto")            ? "linux-aarch64-gnu+crypto"
                             : !strcmp(k, "musl-crypto")     ? "linux-aarch64-musl+crypto"
                             : !strcmp(k, "unknown-profile") ? "host+unmodeled"
                                                             : "linux-unknown-gnu";
        REQUIRE(strstr(RUN(NULL, target, NULL, NULL, NULL, NULL), "unsupported native target"));
    } else if (!strcmp(k, "zero-timeout")) {
        REQUIRE(strstr(RUN(NULL, NULL, "0", NULL, NULL, NULL), "positive representable"));
    } else if (!strcmp(k, "absent-compiler")) {
        REQUIRE(strstr(RUN(language_join(r, root, "absent"), NULL, NULL, NULL, NULL, NULL),
                       "explicit C compiler was not found"));
    } else if (!strcmp(k, "link-failure")) {
        out = RUN(NULL, NULL, NULL, NULL, NULL, ARGS("GOAT_TOOLCHAIN_MODE=link-fail"));
        REQUIRE(strstr(out, "available=0") && strstr(out, "controlled link failure"));
    } else if (!strcmp(k, "hang-query") || !strcmp(k, "hang-link")) {
        out = RUN(NULL, NULL, "1", NULL, NULL, ARGS(string(r, "GOAT_TOOLCHAIN_MODE=%s", k)));
        REQUIRE(strstr(out, "available=0 timeout=1"));
    } else if (!strcmp(k, "avx2") || !strcmp(k, "sha2")) {
        const char *target = host(&s);
        out = RUN(NULL, string(r, "%s+%s", c->target, k), NULL, NULL, NULL, NULL);
        if (strstr(out, "unsupported native target")) {
            if (!strcmp(k, "avx2")) {
                c->skipped = true;
                puts("SKIP: original AVX2 assertion is conditional on supported target");
            }
        } else {
            REQUIRE(strstr(out, string(r, "+%s available=1", k)));
            if (!strcmp(k, "sha2")) {
                out = RUN(NULL, string(r, "%s+sha2", target), NULL, NULL, "list", NULL);
                REQUIRE(strstr(out, string(r, "%s+sha2 available=1", target)));
            }
        }
    } else {
        const char *const *isolated = isolation(&s);
#ifdef _WIN32
        const char *preferred = "gcc" SUFFIX, *fallback = "clang" SUFFIX;
#else
        const char *preferred = "cc", *fallback = "gcc";
#endif
        const char *const *failure =
            environment(r, isolated, ARGS(string(r, "GOAT_TOOLCHAIN_FAIL=%s", preferred)));
        if (!strcmp(k, "path-first")) {
            out = RUN("PATH", NULL, NULL, NULL, NULL, isolated);
            REQUIRE(!strstr(out, "selected=none"));
            const char *line = read_log(&s);
            while (*line) {
                const char *end = strchr(line, '\n');
                if (!end)
                    end = line + strlen(line);
                REQUIRE(strstr(string(r, "%.*s", (int)(end - line), line),
                               string(r, "%s ", preferred)));
                line = *end ? end + 1 : end;
            }
        } else if (!strcmp(k, "path-fallback")) {
            selected(&s, RUN("PATH", NULL, NULL, NULL, NULL, failure), fallback);
        } else if (!strcmp(k, "path-timeout")) {
            out = RUN("PATH",
                      NULL,
                      "2",
                      NULL,
                      NULL,
                      environment(r,
                                  isolated,
                                  ARGS("GOAT_TOOLCHAIN_MODE=hang-query",
                                       string(r, "GOAT_TOOLCHAIN_MODE_FOR=%s", preferred))));
            selected(&s, out, fallback);
            REQUIRE(strstr(out, "timeout=1"));
        } else if (!strcmp(k, "path-unsearched")) {
            out = RUN("PATH",
                      NULL,
                      NULL,
                      NULL,
                      NULL,
                      ARGS(string(r, "PATH=%s", language_join(r, root, "unsearched"))));
            REQUIRE(strstr(out, "no supported C compiler"));
        } else if (!strcmp(k, "path-list")) {
            out = RUN("PATH", NULL, NULL, NULL, "list", isolated);
            REQUIRE(occurrences(out, "available=1") >= 3);
        } else {
            REQUIRE(!strcmp(k, "explicit-failure"));
            out = RUN(language_join(r, root, preferred), NULL, NULL, NULL, NULL, failure);
            REQUIRE(strstr(out, "selected=none") && strstr(out, "count=1"));
        }
    }
#undef RUN
#undef SIMPLE
}

static const char *kinds[] = {"cache",         "reprobe",
                              "linux-relro",   "unicode-temp",
                              "timeout-cache", "unsupported-target",
                              "zero-timeout",  "absent-compiler",
                              "link-failure",  "hang-query",
                              "hang-link",     "avx2",
                              "sha2",          "crypto",
                              "foreign-libc",  "musl-trace",
                              "musl-crypto",   "unknown-profile",
                              "path-first",    "path-fallback",
                              "path-timeout",  "path-unsearched",
                              "path-list",     "explicit-failure"};

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
    int result = 1;
    if (argc == 7 || (argc == 8 && scenario_keep_work_argument(argv[7]))) {
        enum { COUNT = sizeof(kinds) / sizeof(kinds[0]) };

        config_t configs[COUNT];
        scenario_definition_t defs[COUNT];
        char ids[COUNT][96];
        fs_error_t error = {0};
        char *exe = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        if (exe) {
            for (size_t i = 0; i < COUNT; i++) {
                snprintf(ids[i], sizeof(ids[i]), "integration.native_toolchains.%s", kinds[i]);
                configs[i] = (config_t){exe, argv[3], argv[4], kinds[i], false};
                defs[i] = (scenario_definition_t){ids[i], 480000, body, configs + i, NULL};
            }
            scenario_options_t options = {.build_directory = argv[5], .data_directory = argv[6]};
            const char *selection[] = {argv[1], argc == 8 ? argv[7] : NULL};
            result = scenario_registry_main(defs, COUNT, argc == 8 ? 2 : 1, selection, &options);
            if (!result)
                for (size_t i = 0; i < COUNT; i++)
                    if (configs[i].skipped)
                        result = 77;
        }
        FREE(exe);
    } else
        fputs("usage: native_toolchains_scenarios ID HARNESS CC TARGET BUILD DATA [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
