/** @file native_runner_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Manifest-driven real-native differential runner (#383).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/native_runner_policy.h"
#include "yyjson.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})

typedef struct {
    const char *exe, *python, *selftest, *access, *libm, *runtime_strings, *string_methods,
        *saturation, *root, *suite, *checks, *build, *data;
    bool keep;
    yyjson_val *record;
    unsigned command;
} config_t;

typedef struct {
    const void *data;
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

static const char *field(scenario_run_t *r, yyjson_val *obj, const char *name) {
    yyjson_val *v = nr_object_get(obj, name);
    const char *s = yyjson_get_str(v);
    REQUIRE(s && strlen(s) == yyjson_get_len(v));
    return s;
}

static bytes_t read(scenario_run_t *r, const char *path) {
    size_t size;
    const char *p = language_read(r, path, &size);
    return (bytes_t){p, size};
}

static bool is_file(const char *path) {
    if (!path)
        return false;
    fs_info_t info;
    fs_error_t e = {0};
    bool ok = inspect_fs_path(path, true, &info, &e);
    clear_fs_error(&e);
    return ok && info.kind == FS_FILE;
}

static bool exists(scenario_run_t *r, const char *path) {
    fs_info_t info;
    fs_error_t e = {0};
    bool ok = inspect_fs_path(path, true, &info, &e), missing = e.status == FS_NOT_FOUND;
    clear_fs_error(&e);
    REQUIRE(ok || missing);
    return ok;
}

static bool validate_file(const char *relative, bool check, void *user) {
    config_t *c = user;
    if (check && !strcmp(relative, "checks/runner.py"))
        return is_file(c->selftest);
    if (check && !strcmp(relative, "checks/access.py"))
        return is_file(c->access);
    if (check && !strcmp(relative, "checks/libm.py"))
        return is_file(c->libm);
    if (check && !strcmp(relative, "checks/runtime_strings.py"))
        return is_file(c->runtime_strings);
    if (check && !strcmp(relative, "checks/string_method_errors.py"))
        return is_file(c->string_methods);
    if (check && !strcmp(relative, "checks/saturation.py"))
        return is_file(c->saturation);
    char *path = join_fs_path(check ? c->checks : c->suite, relative, NULL);
    bool ok = path && is_file(path);
    FREE(path);
    return ok;
}

static void compare(scenario_run_t *r, bytes_t actual, bytes_t expected, const char *label) {
    bool equal = nr_output_equal(actual.data, actual.size, expected.data, expected.size);
    if (!equal)
        scenario_require_bytes(r,
                               label,
                               expected.data,
                               expected.size,
                               actual.data,
                               actual.size,
                               true);
    scenario_require(r, equal, label);
}

/* Each capture already persists command-N streams before a fail-fast timeout.
 * Deferred aliases preserve the original label.stdout/stderr even in that case. */
typedef struct {
    char *source[2], *destination[2];
} aliases_t;

static bool copy_capture(const char *source, const char *destination) {
    FILE *input = FOPEN(source, "rb");
    if (!input)
        return errno == ENOENT;
    FILE *output = FOPEN(destination, "wb");
    if (!output) {
        fclose(input);
        return false;
    }
    unsigned char bytes[8192];
    size_t n;
    bool ok = true;
    while ((n = fread(bytes, 1, sizeof(bytes), input)))
        if (fwrite(bytes, 1, n, output) != n) {
            ok = false;
            break;
        }
    if (ferror(input))
        ok = false;
    if (fclose(input))
        ok = false;
    if (fclose(output))
        ok = false;
    return ok;
}

static bool save_aliases(void *user) {
    aliases_t *a = user;
    bool ok = true;
    for (size_t i = 0; i < 2; i++) {
        if (!copy_capture(a->source[i], a->destination[i]))
            ok = false;
        free(a->source[i]);
        free(a->destination[i]);
    }
    free(a);
    return ok;
}

static char *copy_text(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

static bool free_json(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool free_plain(void *p) {
    free(p);
    return true;
}

typedef struct {
    child_options_t options;
    scenario_command_result_t result;
} command_t;

static void capture(scenario_run_t *r, void *user) {
    command_t *c = user;
    c->result = scenario_command_capture(r, &c->options);
}

static scenario_command_result_t execute(scenario_run_t *r,
                                         config_t *c,
                                         const char *label,
                                         const char *const *args,
                                         const char *const *env) {
    const char *work = scenario_work_directory(scenario_run_context(r));
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
    language_write(r, language_join(r, work, string(r, "%s.command.json", label)), json, n);
    unsigned index = ++c->command;
    aliases_t *a = calloc(1, sizeof(*a));
    REQUIRE(a);
    const char *streams[] = {"stdout", "stderr"};
    bool valid = true;
    for (size_t i = 0; i < 2; i++) {
        a->source[i] =
            copy_text(language_join(r, work, string(r, "command-%u.%s", index, streams[i])));
        a->destination[i] =
            copy_text(language_join(r, work, string(r, "%s.%s", label, streams[i])));
        valid &= a->source[i] && a->destination[i];
    }
    if (!valid) {
        for (size_t i = 0; i < 2; i++) {
            free(a->source[i]);
            free(a->destination[i]);
        }
        free(a);
        REQUIRE(false);
    }
    scenario_defer(r, save_aliases, a);
    command_t command = {.options = {.executable = args[0],
                                     .arguments = args,
                                     .environment = env,
                                     .working_directory = c->root,
                                     .empty_stdin = true}};
    scenario_with_timeout(r, 300000, capture, &command);
    scenario_command_result_t result = command.result;
    language_write(r, a->destination[0], result.out, result.out_size);
    language_write(r, a->destination[1], result.err, result.err_size);
    scenario_require(r,
                     result.exit_code == 0,
                     string(r, "%s: nonzero status; see %s.stderr", label, label));
    return result;
}

static bytes_t goat(scenario_run_t *r,
                    config_t *c,
                    const char *label,
                    const char *const *tail,
                    const char *const *env) {
    size_t n = 0;
    while (tail[n])
        n++;
    const char **args = language_owned(r, CALLOC((n + 4) * sizeof(*args)));
    args[0] = c->exe;
    args[1] = "--lang";
    args[2] = "en";
    for (size_t i = 0; i < n; i++)
        args[i + 3] = tail[i];
    scenario_command_result_t result = execute(r, c, label, args, env);
    scenario_require(r, result.err_size == 0, string(r, "%s: unexpected stderr", label));
    return (bytes_t){result.out, result.out_size};
}

static void report(scenario_run_t *r, config_t *c, const char *path, bool native) {
    bytes_t bytes = read(r, path);
    char error[512] = {0};
    scenario_require(
        r,
        nr_check_report(bytes.data, bytes.size, c->record, native, error, sizeof(error)),
        error);
}

static size_t count(bytes_t bytes, const char *needle) {
    size_t n = strlen(needle), found = 0;
    if (!n)
        return bytes.size + 1;
    for (size_t i = 0; i <= bytes.size && n <= bytes.size - i;) {
        if (!memcmp((const char *)bytes.data + i, needle, n)) {
            found++;
            i += n;
        } else
            i++;
    }
    return found;
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

static bool truthy(yyjson_val *value) {
    if (!value || yyjson_is_null(value))
        return false;
    if (yyjson_is_bool(value))
        return yyjson_get_bool(value);
    if (yyjson_is_num(value))
        return yyjson_get_num(value) != 0;
    if (yyjson_is_str(value))
        return yyjson_get_len(value) != 0;
    if (yyjson_is_arr(value))
        return yyjson_arr_size(value) != 0;
    return yyjson_obj_size(value) != 0;
}

static void program(scenario_run_t *r, config_t *c) {
    const char *work = scenario_work_directory(scenario_run_context(r)),
               *name = field(r, c->record, "name"), *fixture = language_join(r, c->suite, name),
               *source = language_join(r, work, "program.goat");
    bytes_t original = read(r, language_join(r, fixture, "program.goat"));
    language_write(r, source, original.data, original.size);
    if (nr_object_get(c->record, "example"))
        compare(r,
                original,
                read(r, language_join(r, c->root, field(r, c->record, "example"))),
                "example source");
    const char *vm_report = language_join(r, work, "vm.report");
    bytes_t reference =
        goat(r,
             c,
             "vm",
             ARGS("--optimize", "none", "--native", "off", "--save-native", vm_report, source),
             NULL);
    report(r, c, vm_report, false);
    const char *expected = language_join(r, fixture, "expected_output.txt");
    if (exists(r, expected))
        compare(r, reference, read(r, expected), "VM vs expected");
    yyjson_val *contains = nr_object_get(c->record, "output_contains");
    if (contains) {
        REQUIRE(yyjson_is_arr(contains));
        unsigned char *normalized = language_owned(r, ALLOC(reference.size + 1));
        size_t n = 0;
        const unsigned char *p = reference.data;
        for (size_t i = 0; i < reference.size; i++) {
            if (p[i] == '\r' && i + 1 < reference.size && p[i + 1] == '\n')
                i++;
            normalized[n++] = p[i];
        }
        for (size_t i = 0; i < yyjson_arr_size(contains); i++) {
            yyjson_val *v = yyjson_arr_get(contains, i);
            const char *text = yyjson_get_str(v);
            REQUIRE(text && strlen(text) == yyjson_get_len(v));
            REQUIRE(count((bytes_t){normalized, n}, text) > 0);
        }
    }
    const char *native_report = language_join(r, work, "native.report");
    bytes_t actual = goat(r,
                          c,
                          "native",
                          ARGS("--native", "required", "--save-native", native_report, source),
                          NULL);
    compare(r, actual, reference, "native vs VM");
    report(r, c, native_report, true);
    yyjson_val *generated = nr_object_get(c->record, "generated");
    if (truthy(generated)) {
        REQUIRE(yyjson_is_obj(generated));
        bytes_t code = goat(r, c, "generated", ARGS("--print-c", source), NULL);
        REQUIRE(utf8(code.data, code.size));
        size_t i, max;
        yyjson_val *key, *value;
        yyjson_obj_foreach(generated, i, max, key, value) {
            const char *pattern = yyjson_get_str(key);
            REQUIRE(pattern && strlen(pattern) == yyjson_get_len(key));
            if (value != nr_object_get(generated, pattern))
                continue;
            REQUIRE(yyjson_is_num(value) || yyjson_is_bool(value));
            double required =
                yyjson_is_bool(value) ? (yyjson_get_bool(value) ? 1 : 0) : yyjson_get_num(value);
            size_t occurrences = count(code, pattern);
            if (!*pattern) {
                occurrences = 1;
                const unsigned char *p = code.data;
                for (size_t j = 0; j < code.size; j++)
                    if ((p[j] & 0xc0) != 0x80)
                        occurrences++;
            }
            REQUIRE((double)occurrences >= required);
        }
    }
    if (nr_object_get(c->record, "driver")) {
        goat(r, c, "export", ARGS("--save-c", source), NULL);
        const char *driver = language_join(r, work, "adapter.exe"),
                   *compiler = fs_environment("CC", NULL);
        if (compiler)
            compiler = language_owned(r, (void *)compiler);
        else
            compiler = "gcc";
        const char *driver_source =
            language_join(r, language_join(r, c->checks, name), field(r, c->record, "driver"));
        execute(r,
                c,
                "driver-build",
                ARGS(compiler,
                     "-std=c11",
                     "-O2",
                     "-Wall",
                     "-Wextra",
                     "-Werror",
                     "-DMEMORY_DEBUG",
                     string(r, "-I%s", language_join(r, c->root, "include")),
                     string(r, "-I%s", c->root),
                     string(r, "-I%s", language_join(r, c->root, "src")),
                     driver_source,
                     language_join(r, work, "program.c"),
                     language_join(r, c->root, "src/model/native_services.c"),
                     language_join(r, c->root, "lib/strings/unicode.c"),
                     language_join(r, c->root, "lib/memory/allocate.c"),
                     language_join(r, work, "libgoat_native.a"),
                     "-lm",
                     "-o",
                     driver),
                NULL);
        execute(r, c, "driver", ARGS(driver), NULL);
    }
    bytes_t compiled =
        goat(r, c, "compile", ARGS("--compile", "--native", "required", source), NULL);
    compare(r, compiled, (bytes_t){"", 0}, "compile");
    language_remove(r, source);
    const char *saved_report = language_join(r, work, "saved.report");
    const char *const *env =
        language_environment_override(r, "CC", language_join(r, work, "missing-compiler"));
    bytes_t saved = goat(r,
                         c,
                         "saved",
                         ARGS("--run",
                              "--native",
                              "required",
                              "--save-native",
                              saved_report,
                              language_join(r, work, "program.gbin")),
                         env);
    compare(r, saved, reference, "saved native vs VM");
    report(r, c, saved_report, true);
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
    const char *work = scenario_work_directory(scenario_run_context(r));
    const char *name = field(r, c->record, "name");
    language_write(r, language_join(r, work, "case.name"), name, strlen(name));
    if (nr_object_get(c->record, "check")) {
        const char *check = field(r, c->record, "check");
        if (!strcmp(check, "checks/runner.py"))
            execute(r, c, "check", ARGS(c->selftest, "all", work, c->data), NULL);
        else if (!strcmp(check, "checks/access.py") || !strcmp(check, "checks/libm.py")
                 || !strcmp(check, "checks/runtime_strings.py")
                 || !strcmp(check, "checks/string_method_errors.py")
                 || !strcmp(check, "checks/saturation.py"))
            execute(r,
                    c,
                    "check",
                    ARGS(!strcmp(check, "checks/access.py")                 ? c->access
                         : !strcmp(check, "checks/libm.py")                 ? c->libm
                         : !strcmp(check, "checks/runtime_strings.py")      ? c->runtime_strings
                         : !strcmp(check, "checks/string_method_errors.py") ? c->string_methods
                                                                            : c->saturation,
                         "all",
                         c->exe,
                         c->root,
                         work,
                         c->data,
                         "--keep-work"),
                    NULL);
        else
            execute(r,
                    c,
                    "check",
                    ARGS(c->python, language_join(r, c->checks, check), c->exe, work),
                    NULL);
    } else
        program(r, c);
}

static bool visit(yyjson_val *record, void *user) {
    config_t *c = user;
    const char *name = yyjson_get_str(nr_object_get(record, "name"));
    size_t length = strlen(name) + sizeof("integration.native_functional.");
    char *id = malloc(length);
    if (!id) {
        fputs("Cannot allocate native scenario ID\n", stderr);
        return false;
    }
    snprintf(id, length, "integration.native_functional.%s", name);
    c->record = record;
    c->command = 0;
    scenario_options_t options = {.name = id,
                                  .build_directory = c->build,
                                  .data_directory = c->data,
                                  .keep_work = c->keep};
    bool ok = scenario_execute(&options, 2400000, body, c);
    free(id);
    printf("[%s] %s\n", ok ? " ok " : "fail", name);
    fflush(stdout);
    return ok;
}

static bool selected_noop(yyjson_val *record, void *user) {
    (void)record;
    (void)user;
    return true;
}

static char *joined(const char *root, const char *path) {
    return join_fs_path(root, path, NULL);
}

/* Adapters are built beside the required policy selftest. Keep the public
 * manifest runner CLI and its unchanged logical legacy check paths stable. */
static char *sibling(const char *selftest, const char *name) {
    char *path = absolute_fs_path(selftest, NULL);
    if (!path)
        return NULL;
    char *slash = strrchr(path, '/'), *backslash = strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash))
        slash = backslash;
    if (!slash) {
        FREE(path);
        return NULL;
    }
    slash[1] = 0;
    char *result = joined(path, name);
    FREE(path);
    return result;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
    int result = 1;
    if (argc >= 8) {
        const char **only = calloc((size_t)argc, sizeof(*only));
        size_t only_count = 0;
        bool valid = only != NULL, selection = false;
        const char prefix[] = "integration.native_functional.";
        if (strcmp(argv[1], "all") && strcmp(argv[1], "--list")) {
            if (strncmp(argv[1], prefix, sizeof(prefix) - 1))
                valid = false;
            else
                only[only_count++] = argv[1] + sizeof(prefix) - 1;
        }
        for (int i = 8; valid && i < argc; i++) {
            if (!strcmp(argv[i], "--keep-work"))
                selection = false;
            else if (!strcmp(argv[i], "--only")) {
                if (strcmp(argv[1], "all") || i + 1 == argc || !strncmp(argv[i + 1], "--", 2))
                    valid = false;
                only_count = 0;
                selection = true;
            } else if (selection && strncmp(argv[i], "--", 2))
                only[only_count++] = argv[i];
            else
                valid = false;
        }
        fs_error_t e = {0};
        char *root = absolute_fs_path(argv[5], &e);
        clear_fs_error(&e);
        char *exe = canonical_fs_path(argv[2], &e);
        clear_fs_error(&e);
        char *suite = root ? joined(root, "test/data/native") : NULL,
             *checks = root ? joined(root, "test/src/native") : NULL,
             *manifest = suite ? joined(suite, "list.json") : NULL,
             *build = joined(argv[6], "native-functional");
        void *raw = NULL;
        size_t size = 0;
        bool read = manifest && read_fs_regular_file(manifest, 16 * 1024 * 1024, &raw, &size, &e);
        clear_fs_error(&e);
        yyjson_read_err parse = {0};
        yyjson_doc *doc = read ? yyjson_read_opts(raw, size, 0, NULL, &parse) : NULL;
        yyjson_val *cases = yyjson_doc_get_root(doc);
#ifdef _WIN32
        const char *suffix = ".exe";
#else
        const char *suffix = "";
#endif
        char access_name[64], libm_name[64], runtime_strings_name[64], string_methods_name[64],
            saturation_name[64];
        snprintf(access_name, sizeof(access_name), "native_access_scenarios%s", suffix);
        snprintf(libm_name, sizeof(libm_name), "native_libm_scenarios%s", suffix);
        char *access = sibling(argv[4], access_name), *libm = sibling(argv[4], libm_name);
        snprintf(runtime_strings_name,
                 sizeof(runtime_strings_name),
                 "native_runtime_strings_scenarios%s",
                 suffix);
        snprintf(string_methods_name,
                 sizeof(string_methods_name),
                 "native_string_method_scenarios%s",
                 suffix);
        snprintf(saturation_name, sizeof(saturation_name), "native_saturation_scenarios%s", suffix);
        char *saturation = sibling(argv[4], saturation_name);
        char *runtime_strings = sibling(argv[4], runtime_strings_name),
             *string_methods = sibling(argv[4], string_methods_name);
        config_t c = {.exe = exe,
                      .python = argv[3],
                      .selftest = argv[4],
                      .access = access,
                      .libm = libm,
                      .runtime_strings = runtime_strings,
                      .string_methods = string_methods,
                      .saturation = saturation,
                      .root = root,
                      .suite = suite,
                      .checks = checks,
                      .build = build,
                      .data = argv[7],
                      .keep = true};
        char error[512] = {0};
        if (valid && root && suite && checks && build && doc
            && nr_manifest_validate(cases, validate_file, &c, error, sizeof(error)) && exe
            && is_file(exe)) {
            if (!strcmp(argv[1], "--list")) {
                for (size_t i = 0; i < yyjson_arr_size(cases); i++)
                    printf("integration.native_functional.%s\n",
                           yyjson_get_str(nr_object_get(yyjson_arr_get(cases, i), "name")));
                result = 0;
            } else {
                size_t selected = 0, selection_failed = 0;
                bool selected_ok = nr_run_selected(cases,
                                                   only,
                                                   only_count,
                                                   selected_noop,
                                                   NULL,
                                                   &selected,
                                                   &selection_failed,
                                                   error,
                                                   sizeof(error));
                if (!selected_ok) {
                    fprintf(stderr, "Native selection: %s\n", error);
                }
                fs_info_t info;
                bool available =
                    inspect_fs_path(build, true, &info, &e) && info.kind == FS_DIRECTORY;
                clear_fs_error(&e);
                if (!available && selected_ok) {
                    available = create_fs_directory(build, &e);
                    clear_fs_error(&e);
                }
                if (available && selected_ok) {
                    size_t total = 0, failed = 0;
                    bool ok = nr_run_selected(cases,
                                              only,
                                              only_count,
                                              visit,
                                              &c,
                                              &total,
                                              &failed,
                                              error,
                                              sizeof(error));
                    printf("Native functional testing done; total: %zu, passed: %zu, failed: %zu\n",
                           total,
                           total - failed,
                           failed);
                    result = ok ? 0 : 1;
                }
            }
        }
        if (result && *error)
            fprintf(stderr, "Native runner: %s\n", error);
        else if (result && !doc)
            fprintf(stderr,
                    "Cannot load native manifest: %s\n",
                    parse.msg ? parse.msg : "read failed");
        yyjson_doc_free(doc);
        FREE(raw);
        FREE(manifest);
        FREE(access);
        FREE(libm);
        FREE(runtime_strings);
        FREE(string_methods);
        FREE(saturation);
        FREE(build);
        FREE(checks);
        FREE(suite);
        FREE(exe);
        FREE(root);
        free(only);
    } else
        fputs("usage: native_runner_testing all|ID GOAT PYTHON SELFTEST REPOSITORY BUILD DATA "
              "[--only NAME ...] [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
