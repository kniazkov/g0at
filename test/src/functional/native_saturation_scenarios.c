/** @file native_saturation_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exact independent saturation oracle, frozen operands and original compiler drivers (#386).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/integer_oracle.h"
#include "test/support/language_execution.h"
#include "test/support/native_runner_policy.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <unistd.h>
#endif

#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    const char *goat, *repo, *data, *fixtures, *kind;
} config_t;

typedef struct {
    const unsigned char *data;
    size_t size;
} bytes_t;

typedef struct {
    char *data;
    size_t size, capacity;
} builder_t;

typedef struct {
    int64_t a, b;
} pair_t;

typedef struct {
    const char *source, *driver;
    bytes_t expected;
} corpus_t;

typedef struct {
    child_options_t options;
    scenario_command_result_t result;
} command_t;

static const int64_t edges[] = {INT64_MIN,
                                INT64_MIN + 1,
                                INT64_MIN + 2,
                                -INT64_C(3037000500),
                                -2,
                                -1,
                                0,
                                1,
                                2,
                                INT64_C(3037000499),
                                INT64_C(3037000500),
                                INT64_MAX - 2,
                                INT64_MAX - 1,
                                INT64_MAX};
static const char *valid_names[] =
    {"minimum", "spaced_minimum", "double_negation", "division", "order", "unchanged"};
static const char *kinds[] = {"vm-none-off",
                              "vm-all-off",
                              "vm-all-auto",
                              "vm-all-required",
                              "persisted",
                              "helpers-builtin",
                              "helpers-portable",
                              "valid-minimum",
                              "valid-spaced_minimum",
                              "valid-double_negation",
                              "valid-division",
                              "valid-order",
                              "valid-unchanged",
                              "invalid-0",
                              "invalid-1",
                              "invalid-2",
                              "invalid-3",
                              "invalid-4",
                              "invalid-5",
                              "invalid-6",
                              "invalid-7",
                              "control-output",
                              "control-report",
                              "control-input",
                              "control-oracle",
                              "control-host",
                              "control-invalid",
                              "control-execution",
                              "corpus",
                              "all"};

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

static bytes_t literal(const char *s) {
    return (bytes_t){(const unsigned char *)s, strlen(s)};
}

static bytes_t read_bytes(scenario_run_t *r, const char *path) {
    size_t size;
    const char *p = language_read(r, path, &size);
    return (bytes_t){(const unsigned char *)p, size};
}

static const char *work_path(scenario_run_t *r, const char *name) {
    return language_join(r, scenario_work_directory(scenario_run_context(r)), name);
}

static bool equal(bytes_t a, bytes_t b) {
    return a.size == b.size && !memcmp(a.data, b.data, a.size);
}

static bool utf8(bytes_t bytes) {
    const unsigned char *p = bytes.data;
    for (size_t i = 0; i < bytes.size;) {
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
        if (remaining > bytes.size - i)
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

/* Python subprocess text=True: strict decoding and universal newlines, not trim/splitlines. */
static bytes_t text(scenario_run_t *r, bytes_t bytes) {
    REQUIRE(utf8(bytes));
    unsigned char *out = language_owned(r, ALLOC(bytes.size + 1));
    size_t n = 0;
    for (size_t i = 0; i < bytes.size; i++) {
        if (bytes.data[i] == '\r') {
            out[n++] = '\n';
            if (i + 1 < bytes.size && bytes.data[i + 1] == '\n')
                i++;
        } else
            out[n++] = bytes.data[i];
    }
    out[n] = 0;
    return (bytes_t){out, n};
}

/* Preserve the original Path.write_text newline translation on Windows. */
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

static builder_t builder(scenario_run_t *r, size_t capacity) {
    return (builder_t){language_owned(r, CALLOC(capacity)), 0, capacity};
}

static void append(scenario_run_t *r, builder_t *b, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsnprintf(b->data + b->size, b->capacity - b->size, format, args);
    va_end(args);
    REQUIRE(n >= 0 && (size_t)n < b->capacity - b->size);
    b->size += (size_t)n;
}

static int64_t oracle(integer_oracle_operation_t op, int64_t a, int64_t b) {
    return integer_oracle_saturate(integer_oracle_evaluate(op, a, b));
}

static const char *c_literal(scenario_run_t *r, int64_t value) {
    if (value == INT64_MIN)
        return "(-INT64_C(9223372036854775807)-1)";
    if (value < 0)
        return string(r, "-INT64_C(%" PRId64 ")", -value);
    return string(r, "INT64_C(%" PRId64 ")", value);
}

static bool integer(yyjson_val *v, int64_t *out) {
    if (yyjson_is_sint(v)) {
        *out = yyjson_get_sint(v);
        return true;
    }
    if (yyjson_is_uint(v) && yyjson_get_uint(v) <= INT64_MAX) {
        *out = (int64_t)yyjson_get_uint(v);
        return true;
    }
    return false;
}

static bool pairs_ok(yyjson_val *root, pair_t *out) {
    if (!yyjson_is_arr(root) || yyjson_arr_size(root) != 1000)
        return false;
    for (size_t i = 0; i < 1000; i++) {
        yyjson_val *pair = yyjson_arr_get(root, i);
        int64_t a, b;
        if (!yyjson_is_arr(pair) || yyjson_arr_size(pair) != 2
            || !integer(yyjson_arr_get(pair, 0), &a) || !integer(yyjson_arr_get(pair, 1), &b))
            return false;
        if (out)
            out[i] = (pair_t){a, b};
    }
    return true;
}

static bool free_json(void *doc) {
    yyjson_doc_free(doc);
    return true;
}

static yyjson_doc *json(scenario_run_t *r, bytes_t input) {
    yyjson_doc *doc = yyjson_read((const char *)input.data, input.size, 0);
    REQUIRE(doc);
    scenario_defer(r, free_json, doc);
    return doc;
}

static void frozen(scenario_run_t *r, config_t *c, const char *name, bytes_t actual) {
    bytes_t expected = read_bytes(r, language_join(r, c->fixtures, name));
    scenario_require_bytes(r, name, expected.data, expected.size, actual.data, actual.size, false);
}

static corpus_t corpus(scenario_run_t *r, config_t *c, bool corrupt) {
    pair_t pairs[1196];
    size_t n = 0;
    for (size_t a = 0; a < COUNT(edges); a++)
        for (size_t b = 0; b < COUNT(edges); b++)
            pairs[n++] = (pair_t){edges[a], edges[b]};
    REQUIRE(n == 196);
    yyjson_doc *doc =
        json(r, read_bytes(r, language_join(r, c->data, "python-samples/saturation-pairs.json")));
    REQUIRE(pairs_ok(yyjson_doc_get_root(doc), pairs + n));
    builder_t source = builder(r, 128 * 1024), expected = builder(r, 64 * 1024),
              driver = builder(r, 1024 * 1024);
    append(r,
           &source,
           "const add=func(a,b){return a+b;};const sub=func(a,b){return a-b;};"
           "const mul=func(a,b){return a*b;};const neg=func(a){return -a;};"
           "const mag=func(a){return abs(a);};const inc=func(a){return ++a;};"
           "const dec=func(a){return --a;};"
           "const divmin=func(a){return (-9223372036854775808/-1)+a;};");
    const char *names[] = {"add", "sub", "mul"}, *helpers[] = {"add", "subtract", "multiply"};
    const integer_oracle_operation_t binary[] = {INTEGER_ORACLE_ADD,
                                                 INTEGER_ORACLE_SUB,
                                                 INTEGER_ORACLE_MUL};
    for (size_t i = 0; i < 196; i++) {
        for (size_t op = 0; op < 3; op++) {
            append(r,
                   &source,
                   "\nprintln(%s(%" PRId64 ",%" PRId64 "));",
                   names[op],
                   pairs[i].a,
                   pairs[i].b);
            int64_t value = oracle(binary[op], pairs[i].a, pairs[i].b);
            if (corrupt && i == 0 && op == 0)
                value++;
            append(r, &expected, "%" PRId64 "\n", value);
        }
    }
    const char *unary_names[] = {"neg", "mag", "inc", "dec", "divmin"};
    const integer_oracle_operation_t unary[] = {INTEGER_ORACLE_NEG,
                                                INTEGER_ORACLE_ABS,
                                                INTEGER_ORACLE_INC,
                                                INTEGER_ORACLE_DEC};
    for (size_t i = 0; i < COUNT(edges); i++) {
        for (size_t op = 0; op < 5; op++) {
            append(r, &source, "\nprintln(%s(%" PRId64 "));", unary_names[op], edges[i]);
            int64_t value = op == 4 ? oracle(INTEGER_ORACLE_ADD, INT64_MAX, edges[i])
                                    : oracle(unary[op], edges[i], 0);
            append(r, &expected, "%" PRId64 "\n", value);
        }
    }
    append(r,
           &driver,
           "#include \"saturation.c\"\n#include \"integer_math.h\"\n#include <assert.h>\nint "
           "main(void) {");
    for (size_t i = 0; i < COUNT(pairs); i++) {
        append(r,
               &driver,
               "\n{ volatile int64_t a=%s,b=%s;",
               c_literal(r, pairs[i].a),
               c_literal(r, pairs[i].b));
        for (size_t op = 0; op < 3; op++) {
            const char *value = c_literal(r, oracle(binary[op], pairs[i].a, pairs[i].b));
            append(r,
                   &driver,
                   "\nassert(g_i64_%s(a,b)==%s);\nassert(%s_int64_saturating(a,b)==%s);",
                   names[op],
                   value,
                   helpers[op],
                   value);
        }
        append(r, &driver, "\n}");
    }
    append(r, &driver, "\nreturn 0;\n}");
    bytes_t source_bytes = {(const unsigned char *)source.data, source.size},
            driver_bytes = {(const unsigned char *)driver.data, driver.size},
            expected_bytes = {(const unsigned char *)expected.data, expected.size};
    frozen(r, c, "saturation.goat", source_bytes);
    frozen(r, c, "expected.txt", expected_bytes);
    frozen(r, c, "driver.c.txt", driver_bytes);
    const char *source_path = work_path(r, "saturation.goat"),
               *driver_path = work_path(r, "driver.c");
    write_text(r, source_path, source_bytes);
    write_text(r, driver_path, driver_bytes);
    language_write(r, work_path(r, "expected.txt"), expected.data, expected.size);
    return (corpus_t){source_path, driver_path, expected_bytes};
}

static void capture(scenario_run_t *r, void *user) {
    command_t *command = user;
    command->result = scenario_command_capture(r, &command->options);
    REQUIRE(utf8((bytes_t){command->result.out, command->result.out_size}));
    REQUIRE(utf8((bytes_t){command->result.err, command->result.err_size}));
}

static scenario_command_result_t
execute(scenario_run_t *r, config_t *c, const char *const *args, uint64_t timeout, bool self) {
    command_t command = {
        .options = {.executable = self ? NULL : args[0],
                    .arguments = args,
                    .working_directory = c->repo,
                    .environment = language_environment_override(r, "GOAT_LANGUAGE", "en"),
                    .empty_stdin = true}};
    scenario_with_timeout(r, timeout, capture, &command);
    return command.result;
}

static scenario_command_result_t
run(scenario_run_t *r, config_t *c, const char *const *args, uint64_t timeout) {
    return execute(r, c, args, timeout, false);
}

static scenario_command_result_t
run_probe(scenario_run_t *r, config_t *c, const char *mode, uint64_t timeout) {
    /* NULL executable selects this process through the shared OS implementation,
     * even when the runner itself was found by a bare PATH name. */
    return execute(r, c, ARGS("native_saturation_scenarios", "--probe", mode), timeout, true);
}

static bool output_ok(scenario_command_result_t result, bytes_t actual, bytes_t expected) {
    return result.exit_code == 0 && equal(actual, expected);
}

static void output(scenario_run_t *r, scenario_command_result_t result, bytes_t expected) {
    bytes_t actual = text(r, (bytes_t){result.out, result.out_size});
    REQUIRE(result.exit_code == 0);
    scenario_require_bytes(r,
                           "saturation stdout",
                           expected.data,
                           expected.size,
                           actual.data,
                           actual.size,
                           false);
    REQUIRE(output_ok(result, actual, expected));
}

static void succeeded(scenario_run_t *r, scenario_command_result_t result) {
    REQUIRE(result.exit_code == 0);
}

static void vm(scenario_run_t *r, config_t *c, corpus_t input, size_t mode) {
    const char *optimize[] = {"none", "all", "all", "all"},
               *native[] = {"off", "off", "auto", "required"};
    const char *report = work_path(r, string(r, "%s-%s.report", optimize[mode], native[mode]));
    output(r,
           run(r,
               c,
               ARGS(c->goat,
                    "--optimize",
                    optimize[mode],
                    "--native",
                    native[mode],
                    "--save-native",
                    report,
                    input.source),
               120000),
           input.expected);
    if (mode >= 2) {
        bytes_t raw = read_bytes(r, report);
        REQUIRE(nr_check_saturation_report(raw.data, raw.size));
    }
}

static void persisted(scenario_run_t *r, config_t *c, corpus_t input) {
    succeeded(r,
              run(r, c, ARGS(c->goat, "--compile", "--native", "required", input.source), 120000));
    output(r,
           run(r,
               c,
               ARGS(c->goat, "--run", "--native", "required", work_path(r, "saturation.gbin")),
               120000),
           input.expected);
}

static void export(scenario_run_t *r, config_t *c, corpus_t input) {
    succeeded(r, run(r, c, ARGS(c->goat, "--save-c", input.source), 120000));
}

static const char *
compile(scenario_run_t *r, config_t *c, const char *driver, bool portable, const char *name) {
    char *raw = fs_environment("CC", NULL);
    const char *cc = raw ? language_owned(r, raw) : "gcc";
    const char *exe = work_path(r, name);
    const char *args[16];
    size_t n = 0;
    args[n++] = cc;
    args[n++] = "-std=c11";
    args[n++] = "-O2";
    args[n++] = "-Wall";
    args[n++] = "-Wextra";
    args[n++] = "-Werror";
    if (portable)
        args[n++] = "-DGOAT_FORCE_PORTABLE_INTEGER_MATH";
    args[n++] = string(r, "-I%s", c->repo);
    args[n++] = string(r, "-iquote%s", language_join(r, c->repo, "lib/numbers"));
    args[n++] = driver;
    args[n++] = "-lm";
    args[n++] = "-o";
    args[n++] = exe;
    args[n] = NULL;
    succeeded(r, run(r, c, args, 120000));
    return exe;
}

static void helpers(scenario_run_t *r, config_t *c, corpus_t input, bool portable) {
    const char *exe =
        compile(r, c, input.driver, portable, portable ? "portable.exe" : "builtin.exe");
    succeeded(r, run(r, c, ARGS(exe), 120000));
}

static const char *fixture(scenario_run_t *r, config_t *c, const char *name) {
    const char *path = work_path(r, name);
    write_text(r, path, read_bytes(r, language_join(r, c->fixtures, name)));
    return path;
}

static void valid(scenario_run_t *r, config_t *c, const char *name) {
    const char *path = fixture(r, c, string(r, "%s.goat", name));
    bytes_t expected = read_bytes(r, language_join(r, c->fixtures, string(r, "%s.expected", name)));
    output(r, run(r, c, ARGS(c->goat, "--optimize", "none", path), 120000), expected);
    output(r, run(r, c, ARGS(c->goat, "--optimize", "all", path), 120000), expected);
}

static bool contains(bytes_t bytes, const char *needle) {
    size_t n = strlen(needle);
    for (size_t i = 0; i + n <= bytes.size; i++)
        if (!memcmp(bytes.data + i, needle, n))
            return true;
    return false;
}

static bool invalid_ok(scenario_command_result_t result) {
    return result.exit_code != 0 && contains((bytes_t){result.err, result.err_size}, "64-bit");
}

static scenario_command_result_t invalid(scenario_run_t *r, config_t *c, unsigned index) {
    const char *path = fixture(r, c, string(r, "invalid%u.goat", index));
    scenario_command_result_t result = run(r, c, ARGS(c->goat, path), 30000);
    REQUIRE(invalid_ok(result));
    return result;
}

static bool free_entries(void *resource) {
    fs_entries_t *entries = resource;
    clear_fs_entries(entries);
    FREE(entries);
    return true;
}

static void corrupt_oracle(scenario_run_t *r, void *user) {
    corpus(r, user, true);
    language_write(r, work_path(r, "unexpected-continuation"), "bad", 3);
}

typedef struct {
    config_t *config;
    const char *mode;
    uint64_t command_timeout;
    char *work;
    bool continued;
} execution_control_t;

static void rejected_execution(scenario_run_t *r, void *user) {
    execution_control_t *control = user;
    control->work = absolute_fs_path(scenario_work_directory(scenario_run_context(r)), NULL);
    REQUIRE(control->work);
    scenario_command_result_t result =
        run_probe(r, control->config, control->mode, control->command_timeout);
    output(r, result, literal("ok\n"));
    control->continued = true;
}

static void control_execution(scenario_run_t *r, config_t *c) {
    output(r, run_probe(r, c, "good", 120000), literal("ok\n"));
    const char *modes[] = {"exit", "wrong", "hang", "hang"};
    for (size_t i = 0; i < COUNT(modes); i++) {
        execution_control_t control = {c, modes[i], i == 3 ? 30000 : 120000, NULL, false};
        scenario_options_t options = {.name = "saturation-execution-control",
                                      .build_directory =
                                          scenario_work_directory(scenario_run_context(r)),
                                      .data_directory = c->data,
                                      .keep_work = true};
        bool accepted =
            scenario_execute(&options, i >= 2 ? 5000 : 30000, rejected_execution, &control);
        const char *work = control.work ? language_owned(r, control.work) : NULL;
        REQUIRE(work && !accepted && !control.continued);
        bytes_t log = read_bytes(r, language_join(r, work, "execution.log"));
        bytes_t out = text(r, read_bytes(r, language_join(r, work, "command-1.stdout")));
        bytes_t err = text(r, read_bytes(r, language_join(r, work, "command-1.stderr")));
        REQUIRE(contains(log, "FAIL"));
        REQUIRE(contains(log, "cleanup=0"));
        REQUIRE(equal(err, literal("allowed diagnostic\n")));
        if (i >= 2) {
            REQUIRE(contains(log, string(r, "result status=%d", CHILD_TIMED_OUT)));
            REQUIRE(equal(out, literal("partial\n")));
        } else if (i == 0) {
            REQUIRE(contains(log, "exit=7"));
            REQUIRE(equal(out, literal("ok\n")));
        } else
            REQUIRE(equal(out, literal("wrong\n")));
    }
}

static int probe(const char *mode) {
    fputs("allowed diagnostic\n", stderr);
    fflush(stderr);
    if (!strcmp(mode, "hang")) {
        puts("partial");
        fflush(stdout);
        for (;;) {
#ifdef _WIN32
            Sleep(100);
#else
            usleep(100000);
#endif
        }
    }
    puts(!strcmp(mode, "wrong") ? "wrong" : "ok");
    return !strcmp(mode, "exit") ? 7 : 0;
}

static void control(scenario_run_t *r, config_t *c) {
    if (!strcmp(c->kind, "control-execution")) {
        control_execution(r, c);
        return;
    }
    if (!strcmp(c->kind, "control-report")) {
        const char *good[] = {"succeeded=2\nattempts=2\nretries=0\n",
                              "succeeded=0\nsucceeded=+1_0\vattempts=+1_0\fretries=0\x1e",
                              "succeeded=\xc2\xa0+\xd9\xa1\xc2\xa0\xc2\x85"
                              "attempts=\xc2\xa0+\xd9\xa1\xc2\xa0\xe2\x80\xa8"
                              "retries=0",
                              "mode=irrelevant\nbound=0\npreparation=anything\nsucceeded="
                              "2\rattempts=2\r\nretries=0\n"};
        const char *bad[] = {"succeeded=0\nattempts=0\nretries=0\n",
                             "succeeded=2\nattempts=3\nretries=0\n",
                             "succeeded=2\nattempts=2\nretries=1\n",
                             "succeeded=2\nattempts=2\n",
                             "succeeded=bad\nattempts=bad\nretries=0\n",
                             "succeeded=2\nattempts=2\nretries=0\nbroken\n",
                             "succeeded=2\nattempts=02\nretries=0\n",
                             "succeeded=2\nsucceeded=0\nattempts=0\nretries=0\n",
                             "succeeded=2\nattempts=2\nretries=0\n\xff"};
        for (size_t i = 0; i < COUNT(good); i++)
            REQUIRE(nr_check_saturation_report(good[i], strlen(good[i])));
        for (size_t i = 0; i < COUNT(bad); i++)
            REQUIRE(!nr_check_saturation_report(bad[i], strlen(bad[i])));
    } else if (!strcmp(c->kind, "control-input")) {
        const char *path = language_join(r, c->data, "python-samples/saturation-pairs.json");
        bytes_t raw = read_bytes(r, path);
        REQUIRE(pairs_ok(yyjson_doc_get_root(json(r, raw)), NULL));
        const char *invalid_pair[] = {"[0]",
                                      "[0,0,0]",
                                      "[0.0,0]",
                                      "[9223372036854775808,0]",
                                      "[-9223372036854775809,0]",
                                      "[null,0]",
                                      "[\"0\",0]"};
        for (size_t j = 0; j < COUNT(invalid_pair); j++) {
            builder_t input = builder(r, 128 * 1024);
            append(r, &input, "[%s", invalid_pair[j]);
            for (size_t i = 1; i < 1000; i++)
                append(r, &input, ",[0,0]");
            append(r, &input, "]");
            REQUIRE(!pairs_ok(yyjson_doc_get_root(json(r, literal(input.data))), NULL));
        }
        for (size_t count = 999; count <= 1001; count += 2) {
            builder_t input = builder(r, 128 * 1024);
            append(r, &input, "[");
            for (size_t i = 0; i < count; i++)
                append(r, &input, "%s[0,0]", i ? "," : "");
            append(r, &input, "]");
            REQUIRE(!pairs_ok(yyjson_doc_get_root(json(r, literal(input.data))), NULL));
        }
        REQUIRE(!yyjson_read("[[", 2, 0));
    } else if (!strcmp(c->kind, "control-oracle")) {
        scenario_options_t options = {.name = "saturation-corrupt-oracle",
                                      .build_directory =
                                          scenario_work_directory(scenario_run_context(r)),
                                      .data_directory = c->data,
                                      .keep_work = true};
        REQUIRE(!scenario_execute(&options, 30000, corrupt_oracle, c));
        fs_entries_t *list = CALLOC(sizeof(*list));
        scenario_defer(r, free_entries, list);
        fs_error_t error = {0};
        REQUIRE(list_fs_directory(options.build_directory, list, &error));
        bool observed = false;
        for (size_t i = 0; i < list->count; i++) {
            if (strncmp(list->names[i], "scenario-", 9))
                continue;
            const char *nested = language_join(r, options.build_directory, list->names[i]);
            observed = true;
            REQUIRE(!language_exists(r, language_join(r, nested, "unexpected-continuation")));
            REQUIRE(language_exists(r, language_join(r, nested, "execution.log")));
        }
        clear_fs_error(&error);
        REQUIRE(observed);
    } else if (!strcmp(c->kind, "control-output")) {
        corpus_t input = corpus(r, c, false);
        scenario_command_result_t result =
            run(r, c, ARGS(c->goat, "--optimize", "none", "--native", "off", input.source), 120000);
        output(r, result, input.expected);
        unsigned char *bad = language_owned(r, ALLOC(input.expected.size));
        memcpy(bad, input.expected.data, input.expected.size);
        bad[0] ^= 1;
        REQUIRE(!output_ok(result,
                           text(r, (bytes_t){result.out, result.out_size}),
                           (bytes_t){bad, input.expected.size}));
        REQUIRE(equal(text(r, literal("a\r\nb\rc\n")), literal("a\nb\nc\n")));
        REQUIRE(!equal(text(r, literal("a\n")), literal("a")));
        REQUIRE(!equal(text(r, literal("a\n\n")), literal("a\n")));
        REQUIRE(!equal(text(r, (bytes_t){(const unsigned char *)"a\0b", 3}), literal("a")));
        REQUIRE(!utf8(literal("\xff")));
        result.exit_code = 1;
        REQUIRE(!output_ok(result, input.expected, input.expected));
        result.exit_code = 0;
        result.err = "allowed diagnostic";
        result.err_size = strlen(result.err);
        REQUIRE(output_ok(result, input.expected, input.expected));
    } else if (!strcmp(c->kind, "control-invalid")) {
        scenario_command_result_t result = invalid(r, c, 0);
        result.exit_code = 0;
        REQUIRE(!invalid_ok(result));
        result.exit_code = 1;
        result.err = "other error";
        result.err_size = strlen(result.err);
        REQUIRE(!invalid_ok(result));
        result.err = "\0x64-bit\0";
        result.err_size = 9;
        REQUIRE(invalid_ok(result));
        const char *path = fixture(r, c, "minimum.goat");
        result = run(r, c, ARGS(c->goat, path), 30000);
        REQUIRE(result.exit_code == 0 && !invalid_ok(result));
    } else {
        REQUIRE(!strcmp(c->kind, "control-host"));
        corpus_t input = corpus(r, c, false);
        export(r, c, input);
        bytes_t original = read_bytes(r, language_join(r, c->fixtures, "driver.c.txt"));
        const char *marker = "int main(void) {";
        const char *at = strstr((const char *)original.data, marker);
        REQUIRE(at);
        size_t prefix = (size_t)(at - (const char *)original.data);
        for (size_t helper = 0; helper < 2; helper++) {
            builder_t driver = builder(r, 1024 * 1024);
            append(r,
                   &driver,
                   "%.*s#include <stdlib.h>\n#undef assert\n#define assert(x) "
                   "((x)?(void)0:exit(23))\n#define %s(a,b) INT64_C(0)\n%s",
                   (int)prefix,
                   (const char *)original.data,
                   helper ? "add_int64_saturating" : "g_i64_add",
                   at);
            const char *path = work_path(r, "corrupt-driver.c");
            write_text(r, path, literal(driver.data));
            for (size_t portable = 0; portable < 2; portable++) {
                const char *exe = compile(r, c, path, portable != 0, "corrupt.exe");
                REQUIRE(run(r, c, ARGS(exe), 120000).exit_code == 23);
            }
        }
    }
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
    if (!strncmp(c->kind, "control-", 8)) {
        control(r, c);
        return;
    }
    if (!strncmp(c->kind, "valid-", 6)) {
        valid(r, c, c->kind + 6);
        return;
    }
    if (!strncmp(c->kind, "invalid-", 8)) {
        invalid(r, c, (unsigned)(c->kind[8] - '0'));
        return;
    }
    corpus_t input = corpus(r, c, false);
    if (!strcmp(c->kind, "corpus"))
        return;
    bool all = !strcmp(c->kind, "all");
    for (size_t i = 0; i < 4; i++)
        if (all || !strcmp(c->kind, kinds[i]))
            vm(r, c, input, i);
    if (all || !strcmp(c->kind, "persisted"))
        persisted(r, c, input);
    if (all || !strncmp(c->kind, "helpers-", 8)) {
        export(r, c, input);
        if (all || !strcmp(c->kind, "helpers-builtin"))
            helpers(r, c, input, false);
        if (all || !strcmp(c->kind, "helpers-portable"))
            helpers(r, c, input, true);
    }
    if (all) {
        for (size_t i = 0; i < COUNT(valid_names); i++)
            valid(r, c, valid_names[i]);
        for (unsigned i = 0; i < 8; i++)
            invalid(r, c, i);
    }
}

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
    int result = 1;
    if (argc == 3 && !strcmp(argv[1], "--probe"))
        result = probe(argv[2]);
    else if (argc == 6 || (argc == 7 && scenario_keep_work_argument(argv[6]))) {
        config_t configs[COUNT(kinds)];
        scenario_definition_t defs[COUNT(kinds)];
        char ids[COUNT(kinds)][112];
        char *goat = canonical_fs_path(argv[2], NULL), *repo = canonical_fs_path(argv[3], NULL),
             *data = absolute_fs_path(argv[5], NULL),
             *fixtures = data ? join_fs_path(data, "native-saturation", NULL) : NULL;
        if (goat && repo && data && fixtures) {
            for (size_t i = 0; i < COUNT(kinds); i++) {
                snprintf(ids[i], sizeof(ids[i]), "integration.native_saturation.%s", kinds[i]);
                configs[i] = (config_t){goat, repo, data, fixtures, kinds[i]};
                defs[i] =
                    (scenario_definition_t){ids[i],
                                            !strcmp(kinds[i], "control-host") ? 360000 : 300000,
                                            body,
                                            configs + i,
                                            NULL};
            }
            scenario_options_t options = {.build_directory = argv[4], .data_directory = argv[5]};
            const char *selection[] = {!strcmp(argv[1], "all") ? "integration.native_saturation.all"
                                                               : argv[1],
                                       argc == 7 ? argv[6] : NULL};
            result =
                scenario_registry_main(defs, COUNT(kinds), argc == 7 ? 2 : 1, selection, &options);
        }
        FREE(goat);
        FREE(repo);
        FREE(data);
        FREE(fixtures);
    } else
        fputs("usage: native_saturation_scenarios all|ID|--list GOAT REPOSITORY BUILD DATA "
              "[--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
