/** @file native_access_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Frozen access/retry/compiler fallback scenarios from checks/access.py (#384).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})

static const char expected[] = "200\n3\n200\n1\n42\nACCESS_DENIED\n{\"read\":func}\n";
static const char *kinds[] = {"denied-none",
                              "denied-all",
                              "denied-compile",
                              "retry-off",
                              "retry-auto",
                              "retry-required",
                              "persisted",
                              "compiler-fallback",
                              "control-output",
                              "control-report",
                              "control-fallback",
                              "control-bytecode",
                              "all"};

typedef struct {
    const char *goat, *self, *repo, *build, *data, *kind;
} config_t;

/* Match subprocess text=True: strict UTF-8, then universal newline translation. */
static bool utf8(const unsigned char *p, size_t n) {
    for (size_t i = 0; i < n;) {
        unsigned c = p[i++], v, left, min;
        if (c < 128)
            continue;
        if (c >= 0xc2 && c <= 0xdf) {
            v = c & 31;
            left = 1;
            min = 128;
        } else if (c >= 0xe0 && c <= 0xef) {
            v = c & 15;
            left = 2;
            min = 2048;
        } else if (c >= 0xf0 && c <= 0xf4) {
            v = c & 7;
            left = 3;
            min = 65536;
        } else
            return false;
        if (left > n - i)
            return false;
        while (left--) {
            c = p[i++];
            if ((c & 0xc0) != 0x80)
                return false;
            v = (v << 6) | (c & 63);
        }
        if (v < min || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff))
            return false;
    }
    return true;
}

static char *text(scenario_run_t *r, const void *bytes, size_t size, size_t *length) {
    REQUIRE(utf8(bytes, size));
    const unsigned char *in = bytes;
    char *out = language_owned(r, ALLOC(size + 1));
    size_t used = 0;
    for (size_t i = 0; i < size; i++) {
        unsigned char c = in[i];
        if (c == '\r') {
            if (i + 1 < size && in[i + 1] == '\n')
                i++;
            c = '\n';
        }
        out[used++] = (char)c;
    }
    out[used] = 0;
    *length = used;
    return out;
}

static const char *const *environment(scenario_run_t *r, const char *probe, bool missing) {
    const char *const *base = language_environment_override(r, "GOAT_LANGUAGE", "en");
    size_t n = 0;
    while (base[n])
        n++;
    const char **out = language_owned(r, CALLOC((n + 3) * sizeof(*out)));
    size_t used = 0;
    for (size_t i = 0; i < n; i++) {
#ifdef _WIN32
        bool cc = !_strnicmp(base[i], "CC=", 3),
             control = !_strnicmp(base[i], "GOAT_ACCESS_CONTROL=", 20);
#else
        bool cc = !strncmp(base[i], "CC=", 3),
             control = !strncmp(base[i], "GOAT_ACCESS_CONTROL=", 20);
#endif
        if ((missing && cc) || (probe && control))
            continue;
        out[used++] = base[i];
    }
    if (missing)
        out[used++] = "CC=goat-access-test-missing-compiler";
    if (probe) {
        char *p = language_owned(r, ALLOC(strlen(probe) + 21));
        sprintf(p, "GOAT_ACCESS_CONTROL=%s", probe);
        out[used++] = p;
    }
    return out;
}

typedef struct {
    child_options_t options;
    scenario_command_result_t result;
} command_t;

static void command(scenario_run_t *r, void *user) {
    command_t *c = user;
    c->result = scenario_command_capture(r, &c->options);
}

static scenario_command_result_t
invoke(scenario_run_t *r, config_t *c, const char *const *args, const char *probe, bool missing) {
    command_t call = {.options = {.executable = args[0],
                                  .arguments = args,
                                  .environment = environment(r, probe, missing),
                                  .working_directory = c->repo,
                                  .empty_stdin = true}};
    scenario_with_timeout(r, 120000, command, &call);
    return call.result;
}

static void verify(scenario_run_t *r, bool condition, const char *message) {
    if (!condition)
        fprintf(stderr, "%s\n", message);
    scenario_require(r, condition, message);
}

static void
check(scenario_run_t *r, scenario_command_result_t result, bool success, const char *want) {
    scenario_require(r, (result.exit_code == 0) == success, "access exit status");
    size_t n;
    char *out = text(r, result.out, result.out_size, &n);
    size_t ignored;
    text(r, result.err, result.err_size, &ignored);
    if (n != strlen(want) || memcmp(out, want, n))
        fputs("access stdout\n", stderr);
    scenario_require_bytes(r, "access stdout", want, strlen(want), out, n, false);
}

static bool contains(const void *raw, size_t size, const char *needle) {
    size_t n = strlen(needle);
    const unsigned char *p = raw;
    for (size_t i = 0; n <= size && i <= size - n; i++)
        if (!memcmp(p + i, needle, n))
            return true;
    return false;
}

/* Python splitlines accepts these extra Unicode boundaries, unlike strtok. */
static size_t boundary(const unsigned char *p, size_t n) {
    if (!n)
        return 0;
    if (p[0] == '\n' || p[0] == '\v' || p[0] == '\f' || p[0] == 0x1c || p[0] == 0x1d
        || p[0] == 0x1e)
        return 1;
    if (n >= 2 && p[0] == 0xc2 && p[1] == 0x85)
        return 2;
    if (n >= 3 && p[0] == 0xe2 && p[1] == 0x80 && (p[2] == 0xa8 || p[2] == 0xa9))
        return 3;
    return 0;
}

static void report(scenario_run_t *r, const char *path, bool native) {
    size_t n;
    const char *raw = language_read(r, path, &n);
    char *p = text(r, raw, n, &n);
    bool values[3] = {false, false, false};
    const char *names[] = {"attempts", "succeeded", "retries"}, *wants[] = {"2", "1", "1"};
    for (size_t start = 0; start < n;) {
        size_t end = start;
        while (end < n && !boundary((unsigned char *)p + end, n - end))
            end++;
        const char *eq = memchr(p + start, '=', end - start);
        scenario_require(r, eq != NULL, "access report key=value line");
        for (size_t j = 0; j < 3; j++)
            if ((size_t)(eq - p - start) == strlen(names[j])
                && !memcmp(p + start, names[j], strlen(names[j])))
                values[j] = (size_t)(p + end - eq - 1) == strlen(wants[j])
                            && !memcmp(eq + 1, wants[j], strlen(wants[j]));
        start = end + boundary((unsigned char *)p + end, n - end);
    }
    if (native)
        verify(r, values[0] && values[1] && values[2], "access retry report counters");
}

static const char *fixture(scenario_run_t *r, config_t *c, const char *work, const char *name) {
    size_t n;
    const char *bytes =
        language_read(r, language_join(r, language_join(r, c->data, "native-access"), name), &n);
    const char *path = language_join(r, work, name);
    language_write(r, path, bytes, n);
    return path;
}

static bool exists(scenario_run_t *r, const char *path) {
    fs_error_t e = {0};
    fs_info_t info;
    bool ok = inspect_fs_path(path, true, &info, &e);
    bool absent = e.status == FS_NOT_FOUND;
    clear_fs_error(&e);
    REQUIRE(ok || absent);
    return ok;
}

static void body(scenario_run_t *r, void *user);

static void control(scenario_run_t *r, config_t *c) {
    const char *kind = c->kind + 8;
    const char *target = !strcmp(kind, "output")     ? "integration.native_access.retry-off"
                         : !strcmp(kind, "report")   ? "integration.native_access.retry-auto"
                         : !strcmp(kind, "fallback") ? "integration.native_access.compiler-fallback"
                                                     : "integration.native_access.denied-compile";
    const char *diagnostic = !strcmp(kind, "output")     ? "access stdout"
                             : !strcmp(kind, "report")   ? "access retry report counters"
                             : !strcmp(kind, "fallback") ? "access fallback diagnostic"
                                                         : "access rejected bytecode absent";
    const char *const *args = ARGS(c->self, target, c->self, c->repo, c->build, c->data);
    scenario_command_result_t good = invoke(r, c, args, "good", false);
    scenario_require(r, good.exit_code == 0, "access control baseline must pass");
    scenario_command_result_t bad = invoke(r, c, args, kind, false);
    REQUIRE(bad.exit_code != 0);
    scenario_require(r,
                     contains(bad.err, bad.err_size, diagnostic),
                     "access control must fail intended assertion");
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
    const char *k = c->kind;
    if (!strcmp(k, "all")) {
        for (size_t i = 0; i < 8; i++) {
            config_t one = *c;
            one.kind = kinds[i];
            body(r, &one);
        }
        return;
    }
    if (!strncmp(k, "control-", 8)) {
        control(r, c);
        return;
    }
    const char *work = language_join(r, scenario_work_directory(scenario_run_context(r)), k);
    language_directory(r, work);
    bool denied = !strncmp(k, "denied-", 7);
    const char *source = fixture(r, c, work, denied ? "blocked.goat" : "retry.goat");
    const char *gbin = language_join(r, work, denied ? "blocked.gbin" : "retry.gbin");
    scenario_command_result_t result;
    if (denied) {
        if (!strcmp(k, "denied-compile")) {
            result = invoke(r,
                            c,
                            ARGS(c->goat, "--compile", "--native", "required", source),
                            NULL,
                            false);
            check(r, result, false, "");
            REQUIRE(contains(result.err,
                             result.err_size,
                             "native required needs an own specialization"));
            verify(r, !exists(r, gbin), "access rejected bytecode absent");
        } else {
            result = invoke(r,
                            c,
                            ARGS(c->goat, "--optimize", k + 7, "--native", "off", source),
                            NULL,
                            false);
            check(r, result, true, "ACCESS_DENIED\n");
        }
    } else if (!strncmp(k, "retry-", 6)) {
        const char *path = language_join(r, work, "native.report");
        result = invoke(r,
                        c,
                        ARGS(c->goat, "--native", k + 6, "--save-native", path, source),
                        NULL,
                        false);
        check(r, result, true, expected);
        report(r, path, strcmp(k + 6, "off") != 0);
    } else if (!strcmp(k, "persisted")) {
        result =
            invoke(r, c, ARGS(c->goat, "--compile", "--native", "required", source), NULL, false);
        check(r, result, true, "");
        result = invoke(r, c, ARGS(c->goat, "--run", "--native", "required", gbin), NULL, false);
        check(r, result, true, expected);
    } else {
        REQUIRE(!strcmp(k, "compiler-fallback"));
        result = invoke(r, c, ARGS(c->goat, "--native", "auto", source), NULL, true);
        check(r, result, true, expected);
        verify(r, result.err_size > 0, "access fallback diagnostic");
    }
}

/* A controlled child feeds the real full checker. Baseline success is mandatory,
 * so a launch/cleanup failure cannot be counted as a successful negative control. */
static int probe(int argc, char **argv, const char *mode) {
    const char *save = NULL;
    bool compile = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--compile"))
            compile = true;
        if (!strcmp(argv[i], "--save-native") && i + 1 < argc)
            save = argv[++i];
    }
    if (compile) {
        fputs("native required needs an own specialization\n", stderr);
        if (!strcmp(mode, "bytecode")) {
            char *path = ALLOC(strlen(argv[argc - 1]) + 1);
            strcpy(path, argv[argc - 1]);
            char *dot = strrchr(path, '.');
            if (!dot) {
                FREE(path);
                return 3;
            }
            strcpy(dot, ".gbin");
            FILE *f = FOPEN(path, "wb");
            FREE(path);
            if (!f)
                return 3;
            fputs("forbidden", f);
            if (fclose(f))
                return 3;
        }
        return 1;
    }
    fputs(!strcmp(mode, "output") ? "201\n3\n200\n1\n42\nACCESS_DENIED\n{\"read\":func}\n"
                                  : expected,
          stdout);
    if (save) {
        FILE *f = FOPEN(save, "wb");
        if (!f)
            return 3;
        fputs(!strcmp(mode, "report") ? "attempts=2\nsucceeded=1\nretries=0\n"
                                      : "attempts=2\nsucceeded=1\nretries=1\n",
              f);
        if (fclose(f))
            return 3;
    } else if (strcmp(mode, "fallback"))
        fputs("missing compiler\n", stderr);
    return 0;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
    int result = 1;
    char *mode = fs_environment("GOAT_ACCESS_CONTROL", NULL);
    if (mode && argc > 1 && (!strcmp(argv[1], "--compile") || !strcmp(argv[1], "--native")))
        result = probe(argc, argv, mode);
    else if (argc == 6 || (argc == 7 && scenario_keep_work_argument(argv[6]))) {
        enum { COUNT = sizeof(kinds) / sizeof(kinds[0]) };

        config_t configs[COUNT];
        scenario_definition_t defs[COUNT];
        char ids[COUNT][96];
        fs_error_t error = {0};
        char *goat = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        char *self = absolute_fs_path(argv[0], &error);
        clear_fs_error(&error);
        char *repo = absolute_fs_path(argv[3], &error);
        clear_fs_error(&error);
        char *build = absolute_fs_path(argv[4], &error);
        clear_fs_error(&error);
        char *data = absolute_fs_path(argv[5], &error);
        clear_fs_error(&error);
        if (goat && self && repo && build && data) {
            for (size_t i = 0; i < COUNT; i++) {
                snprintf(ids[i], sizeof(ids[i]), "integration.native_access.%s", kinds[i]);
                configs[i] = (config_t){goat, self, repo, build, data, kinds[i]};
                defs[i] = (scenario_definition_t){ids[i], 300000, body, configs + i, NULL};
            }
            scenario_options_t options = {.build_directory = argv[4], .data_directory = argv[5]};
            const char *selection[] = {!strcmp(argv[1], "all") ? "integration.native_access.all"
                                                               : argv[1],
                                       argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(defs, COUNT, argc == 7 ? 2 : 1, selection, &options);
        }
        FREE(goat);
        FREE(self);
        FREE(repo);
        FREE(build);
        FREE(data);
    } else
        fputs("usage: native_access_scenarios all|ID|--list GOAT REPOSITORY BUILD DATA "
              "[--keep-work]\n",
              stderr);
    FREE(mode);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
