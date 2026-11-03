/** @file native_libm_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Native libm observations, frozen original inputs and independent host oracles (#384).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <stdarg.h>
#include <string.h>

#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})

typedef struct {
    const char *goat, *repo, *fixtures;
    yyjson_val *cases, *item;
    const char *negative;
} config_t;

typedef struct {
    const unsigned char *data;
    size_t size;
} bytes_t;

typedef struct {
    child_options_t options;
    scenario_command_result_t result;
} command_t;

static const char *string(scenario_run_t *r, const char *format, ...) {
    va_list a, b;
    va_start(a, format);
    va_copy(b, a);
    int n = vsnprintf(NULL, 0, format, b);
    va_end(b);
    REQUIRE(n >= 0);
    char *out = language_owned(r, ALLOC((size_t)n + 1));
    vsnprintf(out, (size_t)n + 1, format, a);
    va_end(a);
    return out;
}

static const char *field(scenario_run_t *r, yyjson_val *item, const char *key) {
    yyjson_val *v = yyjson_obj_get(item, key);
    const char *text = yyjson_get_str(v);
    REQUIRE(text && strlen(text) == yyjson_get_len(v));
    return text;
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

/* Python subprocess text=True and Path.read_text use universal newlines. */
static bytes_t text(scenario_run_t *r, const void *data, size_t size) {
    const unsigned char *in = data;
    REQUIRE(utf8(in, size));
    unsigned char *out = language_owned(r, ALLOC(size + 1));
    size_t n = 0;
    for (size_t i = 0; i < size; i++) {
        if (in[i] == '\r') {
            out[n++] = '\n';
            if (i + 1 < size && in[i + 1] == '\n')
                i++;
        } else
            out[n++] = in[i];
    }
    out[n] = 0;
    return (bytes_t){out, n};
}

static bytes_t read(scenario_run_t *r, const char *path) {
    size_t n;
    const char *p = language_read(r, path, &n);
    return (bytes_t){(const unsigned char *)p, n};
}

/* Preserve Path.write_text UTF-8 bytes, including its Windows LF->CRLF translation. */
static void write_text(scenario_run_t *r, const char *path, bytes_t bytes) {
#ifdef _WIN32
    unsigned char *out = language_owned(r, ALLOC(2 * bytes.size + 1));
    size_t n = 0;
    for (size_t i = 0; i < bytes.size; i++) {
        if (bytes.data[i] == '\n')
            out[n++] = '\r';
        out[n++] = bytes.data[i];
    }
    language_write(r, path, out, n);
#else
    language_write(r, path, bytes.data, bytes.size);
#endif
}

static void command(scenario_run_t *r, void *user) {
    command_t *c = user;
    c->result = scenario_command_capture(r, &c->options);
    REQUIRE(utf8(c->result.out, c->result.out_size));
    REQUIRE(utf8(c->result.err, c->result.err_size));
}

static scenario_command_result_t
run(scenario_run_t *r, const char *work, bool english, const char *const *args, uint64_t timeout) {
    command_t c = {
        .options = {.executable = args[0],
                    .arguments = args,
                    .working_directory = work,
                    .environment =
                        english ? language_environment_override(r, "GOAT_LANGUAGE", "en") : NULL,
                    .empty_stdin = true}};
    scenario_with_timeout(r, timeout, command, &c);
    return c.result;
}

static bool output_ok(scenario_command_result_t result, bytes_t actual, bytes_t expected) {
    return result.exit_code == 0 && actual.size == expected.size
           && !memcmp(actual.data, expected.data, actual.size);
}

static bytes_t stdout_text(scenario_run_t *r, scenario_command_result_t result) {
    return text(r, result.out, result.out_size);
}

static void output(scenario_run_t *r, scenario_command_result_t result, bytes_t expected) {
    bytes_t actual = stdout_text(r, result);
    REQUIRE(result.exit_code == 0);
    scenario_require_bytes(r,
                           "libm stdout",
                           expected.data,
                           expected.size,
                           actual.data,
                           actual.size,
                           false);
    REQUIRE(output_ok(result, actual, expected));
}

/* Preserve Python splitlines and int acceptance independently of Goat. The Unicode
 * decimal inventory matches the test-only native_runner_policy oracle. */
static bool scalar(const unsigned char *p, size_t n, size_t *at, uint32_t *value) {
    if (*at >= n)
        return false;
    unsigned c = p[(*at)++], left, min;
    if (c < 128) {
        *value = c;
        return true;
    }
    if (c >= 0xc2 && c <= 0xdf) {
        *value = c & 31;
        left = 1;
        min = 128;
    } else if (c >= 0xe0 && c <= 0xef) {
        *value = c & 15;
        left = 2;
        min = 2048;
    } else if (c >= 0xf0 && c <= 0xf4) {
        *value = c & 7;
        left = 3;
        min = 65536;
    } else
        return false;
    if (left > n - *at)
        return false;
    while (left--) {
        c = p[(*at)++];
        if ((c & 0xc0) != 0x80)
            return false;
        *value = (*value << 6) | (c & 63);
    }
    return *value >= min && *value <= 0x10ffff && !(*value >= 0xd800 && *value <= 0xdfff);
}

static bool linebreak(uint32_t c) {
    return c == 10 || c == 13 || c == 11 || c == 12 || (c >= 28 && c <= 30) || c == 0x85
           || c == 0x2028 || c == 0x2029;
}

static bool intspace(uint32_t c) {
    return c == 9 || c == 10 || c == 11 || c == 12 || c == 13 || c == 32 || c == 0x85 || c == 0xa0
           || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029
           || c == 0x202f || c == 0x205f || c == 0x3000;
}

/* Independent Python Unicode decimal-zero inventory; no production Unicode helper. */
static bool digit(uint32_t c) {
    static const uint32_t zeros[] = {
        0x30,    0x660,   0x6f0,   0x7c0,   0x966,   0x9e6,   0xa66,   0xae6,   0xb66,   0xbe6,
        0xc66,   0xce6,   0xd66,   0xde6,   0xe50,   0xed0,   0xf20,   0x1040,  0x1090,  0x17e0,
        0x1810,  0x1946,  0x19d0,  0x1a80,  0x1a90,  0x1b50,  0x1bb0,  0x1c40,  0x1c50,  0xa620,
        0xa8d0,  0xa900,  0xa9d0,  0xa9f0,  0xaa50,  0xabf0,  0xff10,  0x104a0, 0x10d30, 0x11066,
        0x110f0, 0x11136, 0x111d0, 0x112f0, 0x11450, 0x114d0, 0x11650, 0x116c0, 0x11730, 0x118e0,
        0x11950, 0x11c50, 0x11d50, 0x11da0, 0x11f50, 0x16a60, 0x16ac0, 0x16b50, 0x1d7ce, 0x1d7d8,
        0x1d7e2, 0x1d7ec, 0x1d7f6, 0x1e140, 0x1e2f0, 0x1e4f0, 0x1e950, 0x1fbf0};
    for (size_t i = 0; i < sizeof(zeros) / sizeof(zeros[0]); i++)
        if (c >= zeros[i] && c < zeros[i] + 10)
            return true;
    return false;
}

static bool nonzero_digit(uint32_t c) {
    static const uint32_t zeros[] = {
        0x30,    0x660,   0x6f0,   0x7c0,   0x966,   0x9e6,   0xa66,   0xae6,   0xb66,   0xbe6,
        0xc66,   0xce6,   0xd66,   0xde6,   0xe50,   0xed0,   0xf20,   0x1040,  0x1090,  0x17e0,
        0x1810,  0x1946,  0x19d0,  0x1a80,  0x1a90,  0x1b50,  0x1bb0,  0x1c40,  0x1c50,  0xa620,
        0xa8d0,  0xa900,  0xa9d0,  0xa9f0,  0xaa50,  0xabf0,  0xff10,  0x104a0, 0x10d30, 0x11066,
        0x110f0, 0x11136, 0x111d0, 0x112f0, 0x11450, 0x114d0, 0x11650, 0x116c0, 0x11730, 0x118e0,
        0x11950, 0x11c50, 0x11d50, 0x11da0, 0x11f50, 0x16a60, 0x16ac0, 0x16b50, 0x1d7ce, 0x1d7d8,
        0x1d7e2, 0x1d7ec, 0x1d7f6, 0x1e140, 0x1e2f0, 0x1e4f0, 0x1e950, 0x1fbf0};
    for (size_t i = 0; i < sizeof(zeros) / sizeof(zeros[0]); i++)
        if (c == zeros[i])
            return false;
    return true;
}

static bool positive_integer(const unsigned char *s, size_t size) {
    size_t i = 0, digits = 0;
    uint32_t c;
    bool started = false, negative = false, any = false, nonzero = false, underscore = false,
         ended = false;
    while (i < size) {
        if (!scalar(s, size, &i, &c))
            return false;
        if (!started && intspace(c))
            continue;
        if (!started) {
            started = true;
            if (c == '+' || c == '-') {
                negative = c == '-';
                continue;
            }
        }
        if (ended) {
            if (!intspace(c))
                return false;
            continue;
        }
        if (digit(c)) {
            if (++digits > 4300)
                return false;
            any = true;
            nonzero |= nonzero_digit(c);
            underscore = false;
        } else if (c == '_' && any && !underscore)
            underscore = true;
        else if (intspace(c) && any && !underscore)
            ended = true;
        else
            return false;
    }
    return any && !underscore && !negative && nonzero;
}

typedef struct {
    const unsigned char *p;
    size_t n;
} slice_t;

static bool equals(slice_t v, const char *s) {
    return v.p && v.n == strlen(s) && !memcmp(v.p, s, v.n);
}

static bool report_line(const unsigned char *p, size_t n, slice_t fields[4]) {
    const unsigned char *eq = memchr(p, '=', n);
    if (!eq)
        return false;
    const char *keys[] = {"succeeded", "omitted", "attempts", "retries"};
    for (size_t i = 0; i < 4; i++)
        if ((size_t)(eq - p) == strlen(keys[i]) && !memcmp(p, keys[i], (size_t)(eq - p)))
            fields[i] = (slice_t){eq + 1, n - (size_t)(eq + 1 - p)};
    return true;
}

static bool report_bytes_ok(scenario_run_t *r, bytes_t bytes) {
    (void)r;
    const unsigned char *p = bytes.data;
    size_t i = 0, start = 0;
    slice_t fields[4] = {{0}};
    uint32_t c;
    while (i < bytes.size) {
        size_t pos = i;
        if (!scalar(p, bytes.size, &i, &c))
            return false;
        if (linebreak(c)) {
            if (!report_line(p + start, pos - start, fields))
                return false;
            if (c == 13 && i < bytes.size && p[i] == 10)
                i++;
            start = i;
        }
    }
    if (start < bytes.size && !report_line(p + start, bytes.size - start, fields))
        return false;
    return fields[0].p && fields[2].p && positive_integer(fields[0].p, fields[0].n)
           && equals(fields[1], "0") && equals(fields[3], "0") && fields[0].n == fields[2].n
           && !memcmp(fields[0].p, fields[2].p, fields[0].n);
}

static bool host_ok(scenario_command_result_t result) {
    return result.exit_code == 0;
}

static void compile(scenario_run_t *r, config_t *c, const char *host, const char *exe) {
    char *raw = fs_environment("CC", NULL);
    const char *cc = raw ? language_owned(r, raw) : "gcc";
    const char *include = string(r, "-I%s", language_join(r, c->repo, "include"));
    const char *test_include =
        string(r, "-I%s", language_join(r, c->repo, "test/src/functional/native_abi"));
    scenario_command_result_t result = run(r,
                                           c->repo,
                                           false,
                                           ARGS(cc,
                                                "-std=c11",
                                                include,
                                                test_include,
                                                "-Wall",
                                                "-Wextra",
                                                "-Werror",
                                                "-O2",
                                                host,
                                                "-lm",
                                                "-o",
                                                exe),
                                           60000);
    REQUIRE(result.exit_code == 0);
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
    yyjson_val *item = c->item;
    const char *id = field(r, item, "id"), *kind = field(r, item, "kind");
    const char *work = language_join(r, scenario_work_directory(scenario_run_context(r)), id);
    language_directory(r, work);
    const char *source = field(r, item, "source");
    const char *path = language_join(r,
                                     work,
                                     !strcmp(kind, "host")   ? "edges.goat"
                                     : !strcmp(kind, "zero") ? "zero-contract.goat"
                                                             : source);
    write_text(r, path, read(r, language_join(r, c->fixtures, source)));
    if (!strcmp(kind, "zero")) {
        const char *expected = field(r, item, "expected");
        scenario_command_result_t result = run(r,
                                               c->repo,
                                               true,
                                               ARGS(c->goat,
                                                    "--optimize",
                                                    field(r, item, "optimize"),
                                                    "--native",
                                                    field(r, item, "native"),
                                                    path),
                                               30000);
        output(r, result, (bytes_t){(const unsigned char *)expected, strlen(expected)});
    } else if (!strcmp(kind, "host")) {
        scenario_command_result_t generated =
            run(r, c->repo, true, ARGS(c->goat, "--save-c", path), 30000);
        REQUIRE(generated.exit_code == 0);
        const char *fixture = field(r, item, "host");
        const char *name = string(r, "%.*s", (int)strlen(fixture) - 4, fixture);
        const char *host = language_join(r, work, name);
        write_text(r, host, read(r, language_join(r, c->fixtures, fixture)));
        const char *exe = string(r, "%s/%.*s.exe", work, (int)strlen(name) - 2, name);
        compile(r, c, host, exe);
        REQUIRE(host_ok(run(r, c->repo, false, ARGS(exe), 30000)));
    } else {
        scenario_command_result_t vm =
            run(r, c->repo, true, ARGS(c->goat, "--native", "off", path), 30000);
        REQUIRE(vm.exit_code == 0);
        bytes_t expected = stdout_text(r, vm);
        REQUIRE(expected.size > 0);
        if (!strcmp(kind, "reject"))
            output(r, vm, (bytes_t){(const unsigned char *)"caught\n", 7});
        if (!strcmp(kind, "native")) {
            const char *report = string(r, "%s/%.*s.report", work, (int)strlen(source) - 5, source);
            output(r,
                   run(r,
                       c->repo,
                       true,
                       ARGS(c->goat, "--native", "required", "--save-native", report, path),
                       30000),
                   expected);
            REQUIRE(report_bytes_ok(r, read(r, report)));
        } else {
            REQUIRE(!strcmp(kind, "fallback") || !strcmp(kind, "reject"));
            output(r,
                   run(r, c->repo, true, ARGS(c->goat, "--native", "auto", path), 30000),
                   expected);
        }
    }
}

static const char *replace(scenario_run_t *r, const char *input, const char *from, const char *to) {
    size_t count = 0, a = strlen(from), b = strlen(to);
    for (const char *p = input; (p = strstr(p, from)); p += a)
        count++;
    REQUIRE(count > 0);
    char *out = language_owned(r, ALLOC(strlen(input) + count * b + 1)), *q = out;
    const char *p = input, *next;
    while ((next = strstr(p, from))) {
        size_t n = (size_t)(next - p);
        memcpy(q, p, n);
        q += n;
        memcpy(q, to, b);
        q += b;
        p = next + a;
    }
    strcpy(q, p);
    return out;
}

static void negative(scenario_run_t *r, void *user) {
    config_t *c = user;
    if (!strcmp(c->negative, "output")) {
        bytes_t expected = {(const unsigned char *)"-0\n", 3};
        scenario_command_result_t result = {.exit_code = 0};
        REQUIRE(output_ok(result, expected, expected));
        REQUIRE(!output_ok(result, (bytes_t){(const unsigned char *)"0\n", 2}, expected));
        REQUIRE(!output_ok(result, (bytes_t){(const unsigned char *)"-0\nextra", 8}, expected));
        result.exit_code = 1;
        REQUIRE(!output_ok(result, expected, expected));
    } else if (!strcmp(c->negative, "report")) {
        const char *good = "succeeded=2\nomitted=0\nattempts=2\nretries=0\n";
        REQUIRE(report_bytes_ok(r, (bytes_t){(const unsigned char *)good, strlen(good)}));
        const char *accepted[] = {
            "succeeded=+1\nomitted=0\nattempts=+1\nretries=0\n",
            "succeeded=1_0\vomitted=0\fattempts=1_0\x1c"
            "retries=0\x1d",
            "succeeded=\xc2\xa0+\xd9\xa1\xc2\xa0\xc2\x85"
            "omitted=0\xe2\x80\xa8"
            "attempts=\xc2\xa0+\xd9\xa1\xc2\xa0\xe2\x80\xa9"
            "retries=0",
            "succeeded=0\nsucceeded=2\romitted=0\r\nattempts=2\nretries=0\x1e"};
        for (size_t i = 0; i < sizeof(accepted) / sizeof(accepted[0]); i++)
            REQUIRE(report_bytes_ok(
                r,
                (bytes_t){(const unsigned char *)accepted[i], strlen(accepted[i])}));
        const char *bad[] = {"succeeded=0\nomitted=0\nattempts=0\nretries=0\n",
                             "succeeded=2\nomitted=1\nattempts=2\nretries=0\n",
                             "succeeded=2\nomitted=0\nattempts=3\nretries=0\n",
                             "succeeded=2\nomitted=0\nattempts=2\nretries=1\n",
                             "succeeded=2\nomitted=0\nattempts=2\n",
                             "succeeded=bad\nomitted=0\nattempts=bad\nretries=0\n",
                             "succeeded=2\nomitted=0\nattempts=2\nretries=0\nbroken\n"};
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
            REQUIRE(!report_bytes_ok(r, (bytes_t){(const unsigned char *)bad[i], strlen(bad[i])}));
    } else {
        REQUIRE(!strcmp(c->negative, "host"));
        const char *work = scenario_work_directory(scenario_run_context(r));
        const char *path = language_join(r, work, "edges.goat");
        write_text(r, path, read(r, language_join(r, c->fixtures, "edges-cos.goat")));
        REQUIRE(run(r, c->repo, true, ARGS(c->goat, "--save-c", path), 30000).exit_code == 0);
        bytes_t original = read(r, language_join(r, c->fixtures, "host-cos.c.txt"));
        const char *base = string(r, "%.*s", (int)original.size, original.data);
        /* Same original checks, but normal exit 86 lets the supervisor distinguish
         * an intentional oracle rejection from a crash/timeout/cleanup failure. */
        base = replace(r,
                       base,
                       "#include <assert.h>",
                       "#define assert(x) do { if (!(x)) return 86; } while (0)");
        const char *corrupt = replace(r,
                                      base,
                                      "volatile double expected=",
                                      "r.value.real=123456789.0; volatile double expected=");
        for (unsigned i = 0; i < 2; i++) {
            const char *host = language_join(r, work, i ? "corrupt.c" : "baseline.c");
            const char *exe = language_join(r, work, i ? "corrupt.exe" : "baseline.exe");
            const char *source = i ? corrupt : base;
            write_text(r, host, (bytes_t){(const unsigned char *)source, strlen(source)});
            compile(r, c, host, exe);
            scenario_command_result_t result = run(r, c->repo, false, ARGS(exe), 30000);
            if (!i)
                REQUIRE(host_ok(result));
            else {
                REQUIRE(result.exit_code == 86);
                REQUIRE(!host_ok(result));
            }
        }
    }
}

static void all(scenario_run_t *r, void *user) {
    config_t *c = user;
    size_t i, n;
    yyjson_val *item;
    yyjson_arr_foreach(c->cases, i, n, item) {
        config_t one = *c;
        one.item = item;
        body(r, &one);
    }
}

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
    int result = 1;
    if (argc == 6 || (argc == 7 && scenario_keep_work_argument(argv[6]))) {
        fs_error_t error = {0};
        char *goat = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        char *repo = absolute_fs_path(argv[3], &error);
        clear_fs_error(&error);
        char *fixtures = join_fs_path(argv[5], "native-libm", &error);
        clear_fs_error(&error);
        char *path = fixtures ? join_fs_path(fixtures, "scenarios.json", &error) : NULL;
        clear_fs_error(&error);
        void *data = NULL;
        size_t size = 0;
        bool read_ok = path && read_fs_regular_file(path, 1024 * 1024, &data, &size, &error);
        clear_fs_error(&error);
        yyjson_doc *doc = read_ok ? yyjson_read(data, size, 0) : NULL;
        yyjson_val *cases = yyjson_doc_get_root(doc);
        size_t count = yyjson_arr_size(cases);
        if (goat && repo && yyjson_is_arr(cases) && count == 58) {
            config_t *configs = CALLOC((count + 4) * sizeof(*configs));
            scenario_definition_t *defs = CALLOC((count + 4) * sizeof(*defs));
            const char *negatives[] = {"output", "report", "host"};
            const char *ids[] = {"integration.native_libm.negative-output",
                                 "integration.native_libm.negative-report",
                                 "integration.native_libm.negative-host"};
            for (size_t i = 0; i < count + 4; i++) {
                configs[i] = (config_t){goat, repo, fixtures, cases, NULL, NULL};
                defs[i] = (scenario_definition_t){NULL, 150000, body, configs + i, NULL};
                if (i < count) {
                    configs[i].item = yyjson_arr_get(cases, i);
                    defs[i].id = yyjson_get_str(yyjson_obj_get(configs[i].item, "id"));
                } else if (i < count + 3) {
                    configs[i].negative = negatives[i - count];
                    defs[i].id = ids[i - count];
                    defs[i].body = negative;
                    defs[i].timeout_ms = 240000;
                } else {
                    defs[i].id = "integration.native_libm.all";
                    defs[i].body = all;
                    defs[i].timeout_ms = 300000;
                }
            }
            scenario_options_t options = {.build_directory = argv[4], .data_directory = argv[5]};
            const char *selection[] = {!strcmp(argv[1], "all") ? "integration.native_libm.all"
                                                               : argv[1],
                                       argc == 7 ? argv[6] : NULL};
            result =
                scenario_registry_main(defs, count + 4, argc == 7 ? 2 : 1, selection, &options);
            FREE(defs);
            FREE(configs);
        } else
            fputs("Cannot load native-libm fixtures\n", stderr);
        yyjson_doc_free(doc);
        FREE(data);
        FREE(path);
        FREE(fixtures);
        FREE(repo);
        FREE(goat);
    } else
        fputs(
            "usage: native_libm_scenarios all|ID|--list GOAT REPOSITORY BUILD DATA [--keep-work]\n",
            stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
