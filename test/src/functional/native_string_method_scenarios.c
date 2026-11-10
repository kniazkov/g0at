/** @file native_string_method_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Frozen string-method error, retry and identity scenarios (#385).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/native_runner_policy.h"
#include "test/support/registry.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})

static const char *kinds[] = {"bounds",
                              "types",
                              "identity",
                              "control-output",
                              "control-saved-output",
                              "control-stderr",
                              "control-exit",
                              "control-mode",
                              "control-preparation",
                              "control-bound",
                              "control-attempts",
                              "control-succeeded",
                              "control-retries",
                              "control-report-syntax",
                              "control-policy",
                              "all"};
static const char *keys[] = {"mode", "preparation", "bound", "attempts", "succeeded", "retries"};
static const char *bounds[] = {"required", "ready", "3", "10", "4", "6"};
/* #503 also enters bad/missing string calls through continuation drivers. Their
 * original exceptions still propagate, so only the valid call succeeds. */
static const char *types[] = {"required", "ready", "3", "3", "1", "0"};
/* The shadowed trim function and its plain caller now have native continuations,
 * alongside the existing typed string method. All three complete successfully. */
static const char *identity[] = {"required", "ready", "3", "3", "3", "0"};

typedef struct {
    const char *goat, *self, *repo, *build, *data, *kind;
    unsigned command;
} config_t;

typedef struct {
    child_options_t options;
    scenario_command_result_t result;
} command_t;

typedef struct {
    const unsigned char *data;
    size_t size;
} bytes_t;

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

static bool contains(const void *raw, size_t size, const char *needle) {
    const unsigned char *p = raw;
    size_t n = strlen(needle);
    for (size_t i = 0; n <= size && i <= size - n; i++)
        if (!memcmp(p + i, needle, n))
            return true;
    return false;
}

/* Path.read_text is strict UTF-8 on the original supported CI platforms. Its
 * universal-newline translation is equivalent to consuming CRLF as one break. */
static bool scalar(const unsigned char *p, size_t n, size_t *at, uint32_t *value) {
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

static bool line(const unsigned char *p, size_t n, bytes_t fields[6]) {
    const unsigned char *eq = memchr(p, '=', n);
    if (!eq)
        return false;
    for (size_t i = 0; i < 6; i++)
        if ((size_t)(eq - p) == strlen(keys[i]) && !memcmp(p, keys[i], (size_t)(eq - p)))
            fields[i] = (bytes_t){eq + 1, n - (size_t)(eq + 1 - p)};
    return true;
}

/* Independent exact-string oracle from the Python dictionary, not Goat's report
 * implementation. nr_check_report is intentionally inapplicable: this test has
 * retries, and its bound and counters must match exact strings, not integers. */
static const char *report_error(const void *raw, size_t n, const char *const wanted[6]) {
    const unsigned char *p = raw;
    bytes_t fields[6] = {{0}};
    size_t at = 0, start = 0;
    while (at < n) {
        size_t pos = at;
        uint32_t c;
        if (!scalar(p, n, &at, &c))
            return "string methods report syntax";
        if (linebreak(c)) {
            if (!line(p + start, pos - start, fields))
                return "string methods report syntax";
            if (c == 13 && at < n && p[at] == 10)
                at++;
            start = at;
        }
    }
    if (start < n && !line(p + start, n - start, fields))
        return "string methods report syntax";
    for (size_t i = 0; i < 6; i++)
        if (!fields[i].data || fields[i].size != strlen(wanted[i])
            || memcmp(fields[i].data, wanted[i], fields[i].size))
            return keys[i];
    return NULL;
}

static void report(scenario_run_t *r, const char *path, const char *const wanted[6]) {
    size_t n;
    const char *raw = language_read(r, path, &n);
    const char *error = report_error(raw, n, wanted);
    if (error)
        fprintf(stderr, "string methods report: %s\n", error);
    scenario_require(r, error == NULL, "string methods report");
}

/* Keep Python's named diagnostics even when command capture fails fast. The
 * shared executor first persists command-N streams; this deferred copy runs
 * before scenario-owned paths are freed, including timeout/cleanup failures. */
typedef struct {
    const char *source[2], *destination[2];
} aliases_t;

static bool save_aliases(void *user) {
    aliases_t *a = user;
    bool ok = true;
    for (size_t i = 0; i < 2; i++) {
        FILE *input = FOPEN(a->source[i], "rb");
        if (!input) {
            if (errno != ENOENT)
                ok = false;
            continue;
        }
        FILE *output = FOPEN(a->destination[i], "wb");
        if (!output) {
            fclose(input);
            ok = false;
            continue;
        }
        unsigned char buffer[8192];
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), input)))
            if (fwrite(buffer, 1, n, output) != n) {
                ok = false;
                break;
            }
        if (ferror(input))
            ok = false;
        if (fclose(input))
            ok = false;
        if (fclose(output))
            ok = false;
    }
    return ok;
}

static bool free_json(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool free_plain(void *p) {
    free(p);
    return true;
}

static void diagnostics(scenario_run_t *r,
                        config_t *c,
                        const char *work,
                        const char *label,
                        const char *const *args) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    REQUIRE(doc);
    scenario_defer(r, free_json, doc);
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    REQUIRE(array);
    yyjson_mut_doc_set_root(doc, array);
    for (size_t i = 0; args[i]; i++)
        REQUIRE(yyjson_mut_arr_add_strcpy(doc, array, args[i]));
    size_t n;
    char *json = yyjson_mut_write(doc, 0, &n);
    REQUIRE(json);
    scenario_defer(r, free_plain, json);
    language_write(r, string(r, "%s/%s.command.json", work, label), json, n);
    aliases_t *aliases = language_owned(r, ALLOC(sizeof(*aliases)));
    const char *root = scenario_work_directory(scenario_run_context(r));
    const char *streams[] = {"stdout", "stderr"};
    for (size_t i = 0; i < 2; i++) {
        aliases->source[i] = string(r, "%s/command-%u.%s", root, c->command + 1, streams[i]);
        aliases->destination[i] = string(r, "%s/%s.%s", work, label, streams[i]);
    }
    scenario_defer(r, save_aliases, aliases);
}

static void command(scenario_run_t *r, void *user) {
    command_t *c = user;
    c->result = scenario_command_capture(r, &c->options);
}

static scenario_command_result_t
invoke(scenario_run_t *r, config_t *c, const char *const *args, const char *const *env) {
    command_t cmd = {.options = {.executable = args[0],
                                 .arguments = args,
                                 .environment = env,
                                 .working_directory = c->repo,
                                 .empty_stdin = true}};
    c->command++;
    scenario_with_timeout(r, 300000, command, &cmd);
    return cmd.result;
}

static scenario_command_result_t execute(scenario_run_t *r,
                                         config_t *c,
                                         const char *work,
                                         const char *label,
                                         const char *const *args,
                                         const char *const *env) {
    diagnostics(r, c, work, label, args);
    scenario_command_result_t result = invoke(r, c, args, env);
    language_write(r, string(r, "%s/%s.stdout", work, label), result.out, result.out_size);
    language_write(r, string(r, "%s/%s.stderr", work, label), result.err, result.err_size);
    if (result.exit_code != 0)
        fprintf(stderr, "string methods exit: %s\n", label);
    scenario_require(r, result.exit_code == 0, "string methods exit");
    if (result.err_size)
        fprintf(stderr, "string methods stderr: %s\n", label);
    scenario_require(r, result.err_size == 0, "string methods stderr");
    return result;
}

static void
output(scenario_run_t *r, scenario_command_result_t result, scenario_command_result_t reference) {
    bool ok = nr_output_equal(result.out, result.out_size, reference.out, reference.out_size);
    if (!ok)
        fputs("string methods stdout\n", stderr);
    scenario_require_bytes(r,
                           "string methods stdout",
                           reference.out,
                           reference.out_size,
                           result.out,
                           result.out_size,
                           true);
    scenario_require(r, ok, "string methods stdout policy");
}

/* Freeze the actual original Path.write_text bytes, including leading/trailing
 * empty lines and Windows' exact LF -> CRLF translation. */
static const char *source(scenario_run_t *r, config_t *c, const char *work) {
    size_t n;
    const char *raw =
        language_read(r, string(r, "%s/native-string-methods/%s.goat", c->data, c->kind), &n);
#ifdef _WIN32
    char *translated = language_owned(r, ALLOC(2 * n + 1));
    size_t used = 0;
    for (size_t i = 0; i < n; i++) {
        if (raw[i] == '\n')
            translated[used++] = '\r';
        translated[used++] = raw[i];
    }
    raw = translated;
    n = used;
#endif
    const char *path = language_join(r, work, "program.goat");
    language_write(r, path, raw, n);
    language_write(r, language_join(r, work, "program.goat.input"), raw, n);
    return path;
}

static void policy(scenario_run_t *r) {
    /* Output is binary, not text=True: invalid UTF-8 and embedded NUL are legal;
     * only CRLF pairs normalize, not lone CR, whitespace or trailing lines. */
    const char a[] = " \xff\0\n\n", b[] = " \xff\0\r\n\r\n";
    REQUIRE(nr_output_equal(a, sizeof(a) - 1, b, sizeof(b) - 1));
    REQUIRE(!nr_output_equal("x\r", 2, "x\n", 2));
    REQUIRE(!nr_output_equal("x \n", 3, "x\n", 2));
    REQUIRE(!nr_output_equal("x\n\n", 3, "x\n", 2));
    REQUIRE(!nr_output_equal("x\0", 2, "x", 1));
    const char *accepted[] = {
        "mode=required\npreparation=ready\nbound=3\nattempts=10\nsucceeded=4\nretries=6\n",
        "mode=wrong\rmode=required\r\npreparation=ready\vbound=3\fattempts=10\x1c"
        "succeeded=4\x1d"
        "retries=6\x1e",
        "mode=required\xc2\x85preparation=ready\xe2\x80\xa8"
        "bound=3\xe2\x80\xa9"
        "attempts=10\nsucceeded=4\nretries=6",
        "=\nunknown=a=b\nmode=required\npreparation=ready\nbound=3\nattempts=10\nsucceeded="
        "4\nretries=6\n"};
    for (size_t i = 0; i < sizeof(accepted) / sizeof(*accepted); i++)
        REQUIRE(report_error(accepted[i], strlen(accepted[i]), bounds) == NULL);
    const char *bad[] = {"\n",
                         "broken",
                         "\xff",
                         "x=\xc0\x80",
                         "x=\xed\xa0\x80",
                         "x=\xf4\x90\x80\x80",
                         "x=\xe2\x80"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++)
        REQUIRE(report_error(bad[i], strlen(bad[i]), bounds) != NULL);
    const char *good = accepted[0];
    const char *suffixes[] = {"broken\n",
                              "\n",
                              "mode=off\n",
                              "preparation=failed\n",
                              "bound=03\n",
                              "attempts=+10\n",
                              "succeeded=4 \n",
                              "retries=6=\n",
                              "unknown=\xff\n"};
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(*suffixes); i++) {
        const char *bad_report = string(r, "%s%s", good, suffixes[i]);
        REQUIRE(report_error(bad_report, strlen(bad_report), bounds) != NULL);
    }
    for (size_t omitted = 0; omitted < 6; omitted++) {
        const char *missing = "";
        for (size_t j = 0; j < 6; j++)
            if (j != omitted)
                missing = string(r, "%s%s=%s\n", missing, keys[j], bounds[j]);
        REQUIRE(report_error(missing, strlen(missing), bounds) != NULL);
    }
}

static void control(scenario_run_t *r, config_t *c) {
    const char *kind = c->kind + 8;
    if (!strcmp(kind, "policy"))
        policy(r);
    const char *const *args = ARGS(c->self,
                                   "integration.native_string_methods.bounds",
                                   c->self,
                                   c->repo,
                                   c->build,
                                   c->data);
    scenario_command_result_t good =
        invoke(r, c, args, language_environment_override(r, "GOAT_STRING_METHOD_CONTROL", "good"));
    REQUIRE(good.exit_code == 0);
    if (!strcmp(kind, "policy")) {
        good = invoke(r,
                      c,
                      args,
                      language_environment_override(r, "GOAT_STRING_METHOD_CONTROL", "policy"));
        REQUIRE(good.exit_code == 0);
        return;
    }
    bool report_field = false;
    for (size_t i = 0; i < 6; i++)
        report_field |= !strcmp(kind, keys[i]);
    bool all_stages = !strcmp(kind, "stderr") || !strcmp(kind, "exit");
    const char *stages[] = {"vm", "compile", "native", "saved"};
    size_t count = all_stages ? 4 : report_field ? 2 : 1;
    for (size_t i = 0; i < count; i++) {
        const char *mode = all_stages     ? string(r, "%s-%s", kind, stages[i])
                           : report_field ? string(r, "%s-%s", kind, i ? "saved" : "native")
                                          : kind;
        const char *diagnostic = report_field ? string(r, "string methods report: %s", kind)
                                 : !strcmp(kind, "report-syntax") ? "string methods report syntax"
                                 : all_stages ? string(r, "string methods %s:", kind)
                                              : "string methods stdout";
        scenario_command_result_t bad =
            invoke(r,
                   c,
                   args,
                   language_environment_override(r, "GOAT_STRING_METHOD_CONTROL", mode));
        REQUIRE(bad.exit_code != 0);
        scenario_require(r,
                         contains(bad.err, bad.err_size, diagnostic),
                         "string methods control must fail its intended assertion");
    }
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
    if (!strcmp(c->kind, "all")) {
        for (size_t i = 0; i < 3; i++) {
            config_t one = *c;
            one.kind = kinds[i];
            body(r, &one);
            c->command = one.command;
        }
        return;
    }
    if (!strncmp(c->kind, "control-", 8)) {
        control(r, c);
        return;
    }
    const char *work = language_join(r, scenario_work_directory(scenario_run_context(r)), c->kind);
    language_directory(r, work);
    const char *program = source(r, c, work);
    const char *gbin = language_join(r, work, "program.gbin");
    const char *const *wanted = !strcmp(c->kind, "bounds")  ? bounds
                                : !strcmp(c->kind, "types") ? types
                                                            : identity;
    scenario_command_result_t reference =
        execute(r,
                c,
                work,
                "vm",
                ARGS(c->goat, "--lang", "en", "--native", "off", "--optimize", "none", program),
                NULL);
    execute(r,
            c,
            work,
            "compile",
            ARGS(c->goat, "--lang", "en", "--compile", "--native", "required", program),
            NULL);
    const char *native_report = language_join(r, work, "native.report");
    output(r,
           execute(r,
                   c,
                   work,
                   "native",
                   ARGS(c->goat,
                        "--lang",
                        "en",
                        "--native",
                        "required",
                        "--save-native",
                        native_report,
                        program),
                   NULL),
           reference);
    report(r, native_report, wanted);
    language_remove(r, program);
    const char *saved_report = language_join(r, work, "saved.report");
    const char *missing = language_join(r, work, "missing-compiler");
    output(r,
           execute(r,
                   c,
                   work,
                   "saved",
                   ARGS(c->goat,
                        "--lang",
                        "en",
                        "--run",
                        "--native",
                        "required",
                        "--save-native",
                        saved_report,
                        gbin),
                   language_environment_override(r, "CC", missing)),
           reference);
    report(r, saved_report, wanted);
}

/* Independent command producer, never a production-language oracle. Controls
 * execute the full checker with this producer as GOAT. A passing baseline and
 * the intended diagnostic are mandatory, so cleanup/launch errors cannot pass. */
static int probe(int argc, char **argv, const char *mode) {
    const char *save = NULL;
    bool compile = false, saved = false;
    for (int i = 1; i < argc; i++) {
        compile |= !strcmp(argv[i], "--compile");
        saved |= !strcmp(argv[i], "--run");
        if (!strcmp(argv[i], "--save-native") && i + 1 < argc)
            save = argv[++i];
    }
    const char *phase = compile ? "compile" : saved ? "saved" : save ? "native" : "vm";
    const char *path = argv[argc - 1];
    char *source_path = ALLOC(strlen(path) + 1);
    strcpy(source_path, path);
    char *dot = strrchr(source_path, '.');
    if (!dot) {
        FREE(source_path);
        return 3;
    }
    strcpy(dot, ".goat");
    FILE *f = FOPEN(source_path, "rb");
    if ((f != NULL) == saved) {
        if (f)
            fclose(f);
        FREE(source_path);
        return 3;
    }
    if (f)
        fclose(f);
    FREE(source_path);
    if (saved) {
        f = FOPEN(path, "rb");
        if (!f)
            return 3;
        bool present = fgetc(f) == 'B';
        if (fclose(f))
            present = false;
        char *missing = ALLOC(strlen(path) + 32);
        strcpy(missing, path);
        char *slash = strrchr(missing, '/');
#ifdef _WIN32
        char *backslash = strrchr(missing, '\\');
        if (!slash || (backslash && backslash > slash))
            slash = backslash;
#endif
        if (!slash) {
            FREE(missing);
            return 3;
        }
        strcpy(slash + 1, "missing-compiler");
        char *cc = fs_environment("CC", NULL);
        bool correct_cc = cc && !strcmp(cc, missing);
        FREE(cc);
        FREE(missing);
        if (!present || !correct_cc)
            return 3;
    }
    char selected[96];
    snprintf(selected, sizeof(selected), "exit-%s", phase);
    if (!strcmp(mode, selected))
        return 9;
    snprintf(selected, sizeof(selected), "stderr-%s", phase);
    if (!strcmp(mode, selected))
        fputs("unexpected diagnostic\n", stderr);
    if (compile) {
        char *binary = ALLOC(strlen(path) + 1);
        strcpy(binary, path);
        strcpy(strrchr(binary, '.'), ".gbin");
        f = FOPEN(binary, "wb");
        FREE(binary);
        if (!f)
            return 3;
        fputs("B", f);
        return fclose(f) ? 3 : 0;
    }
#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1)
        return 3;
#endif
    const char out[] = "method\0\xff \n\n", crlf[] = "method\0\xff \r\n\r\n";
    bool corrupt =
        (!strcmp(mode, "output") && save && !saved) || (!strcmp(mode, "saved-output") && saved);
    if (corrupt)
        fputs("changed", stdout);
    if (!strcmp(mode, "policy") && save)
        fwrite(crlf, 1, sizeof(crlf) - 1, stdout);
    else
        fwrite(out, 1, sizeof(out) - 1, stdout);
    if (save) {
        f = FOPEN(save, "wb");
        if (!f)
            return 3;
        if (!strcmp(mode, "policy"))
            fputs("mode=wrong\rmode=required\r\npreparation=ready\vbound=3\f"
                  "attempts=10\x1c"
                  "succeeded=4\xc2\x85retries=6\xe2\x80\xa8"
                  "unknown=a=b\xe2\x80\xa9",
                  f);
        else {
            for (size_t i = 0; i < 6; i++) {
                snprintf(selected, sizeof(selected), "%s-%s", keys[i], phase);
                fprintf(f, "%s=%s\n", keys[i], !strcmp(mode, selected) ? "wrong" : bounds[i]);
            }
            if (!strcmp(mode, "report-syntax"))
                fputs("broken\n", f);
        }
        if (fclose(f))
            return 3;
    }
    return ferror(stdout) ? 3 : 0;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
    int result = 1;
    char *mode = fs_environment("GOAT_STRING_METHOD_CONTROL", NULL);
    if (mode && argc > 1 && !strcmp(argv[1], "--lang"))
        result = probe(argc, argv, mode);
    else if (argc == 6 || (argc == 7 && scenario_keep_work_argument(argv[6]))) {
        enum { COUNT = sizeof(kinds) / sizeof(*kinds) };

        config_t configs[COUNT];
        scenario_definition_t defs[COUNT];
        char ids[COUNT][96];
        fs_error_t error = {0};
        char *goat = canonical_fs_path(argv[2], &error);
        clear_fs_error(&error);
        char *self = canonical_fs_path(argv[0], &error);
        clear_fs_error(&error);
        char *repo = canonical_fs_path(argv[3], &error);
        clear_fs_error(&error);
        char *build = canonical_fs_path(argv[4], &error);
        clear_fs_error(&error);
        char *data = canonical_fs_path(argv[5], &error);
        clear_fs_error(&error);
        if (goat && self && repo && build && data) {
            for (size_t i = 0; i < COUNT; i++) {
                snprintf(ids[i], sizeof(ids[i]), "integration.native_string_methods.%s", kinds[i]);
                configs[i] = (config_t){goat, self, repo, build, data, kinds[i], 0};
                defs[i] = (scenario_definition_t){ids[i], 300000, body, configs + i, NULL};
            }
            scenario_options_t options = {.build_directory = build, .data_directory = data};
            const char *selection[] = {
                !strcmp(argv[1], "all") ? "integration.native_string_methods.all" : argv[1],
                argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(defs, COUNT, argc == 7 ? 2 : 1, selection, &options);
        }
        FREE(goat);
        FREE(self);
        FREE(repo);
        FREE(build);
        FREE(data);
    } else
        fputs("usage: native_string_method_scenarios all|ID|--list GOAT REPOSITORY BUILD DATA "
              "[--keep-work]\n",
              stderr);
    FREE(mode);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
