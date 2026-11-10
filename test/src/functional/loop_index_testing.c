/** @file loop_index_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Loop/index fixture, source round-trip and persisted-native scenarios.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *exe;
    bool index;
} state_t;

#define COMMAND(...)                                                                               \
    language_command(run,                                                                          \
                     state->exe,                                                                   \
                     (const char *const[]){state->exe, __VA_ARGS__, NULL},                         \
                     environment)
#define CHECK(result, text, size) language_check(run, result, 0, text, size, "", 0)

static bool release_entries(void *p) {
    clear_fs_entries(p);
    FREE(p);
    return true;
}

static int compare_names(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static bool selected(const char *name, bool index) {
    static const char *prefixes[] = {"index_",
                                     "array_",
                                     "inheritance_",
                                     "loops_",
                                     "switch_",
                                     "compound_",
                                     "iteration_",
                                     "flat_",
                                     "conditional_",
                                     "finally_",
                                     "access_"};
    if (!index)
        return !strncmp(name, "for_", 4);
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(*prefixes); ++i)
        if (!strncmp(name, prefixes[i], strlen(prefixes[i])))
            return true;
    return false;
}

static void fixtures(scenario_run_t *run, state_t *state) {
    const char *data =
        language_join(run, scenario_data_directory(scenario_run_context(run)), "functional");
    const char *work = scenario_work_directory(scenario_run_context(run));
    const char *const *environment = language_environment(run, state->index);
    fs_entries_t *entries = CALLOC(sizeof(*entries));
    scenario_defer(run, release_entries, entries);
    fs_error_t error = {0};
    bool ok = list_fs_directory(data, entries, &error);
    clear_fs_error(&error);
    scenario_require(run, ok, "cannot enumerate language fixtures");
    qsort(entries->names, entries->count, sizeof(*entries->names), compare_names);
    size_t count = 0;
    for (size_t i = 0; i < entries->count; ++i) {
        const char *name = entries->names[i];
        if (!selected(name, state->index))
            continue;
        const char *fixture = language_join(run, data, name);
        const char *source = language_join(run, fixture, "program.goat");
        if (!language_exists(run, source))
            continue;
        const char *error_path = language_join(run, fixture, "expected_error.txt");
        bool rejected = language_exists(run, error_path);
        size_t size, source_size;
        const char *expected = language_read(
            run,
            rejected ? error_path : language_join(run, fixture, "expected_output.txt"),
            &size);
        const char *code = language_read(run, source, &source_size);
        const char *fixture_work = language_join(run, work, name);
        language_directory(run, fixture_work);
        for (size_t mode = 0; mode < 2; ++mode) {
            const char *optimization = mode ? "all" : "none";
            const char *directory = language_join(run, fixture_work, optimization);
            language_directory(run, directory);
            const char *copied = language_join(run, directory, "program.goat");
            const char *binary = language_join(run, directory, "program.gbin");
            language_write(run, copied, code, source_size);
            if (rejected) {
                scenario_command_result_t result =
                    state->index
                        ? COMMAND("--lang", "en", "--compile", "--optimize", optimization, copied)
                        : COMMAND("--lang", "en", "--optimize", optimization, copied);
                language_check(run, result, 1, "", 0, expected, size);
                scenario_require(run,
                                 !language_exists(run, binary),
                                 "rejected source published bytecode");
                continue;
            }
            CHECK(COMMAND("--lang", "en", "--optimize", optimization, copied), expected, size);
            scenario_command_result_t printed = COMMAND("--lang",
                                                        "en",
                                                        "--optimize",
                                                        optimization,
                                                        "--compile",
                                                        "--print-source-code",
                                                        copied);
            scenario_require(run,
                             printed.exit_code == 0 && !printed.err_size,
                             "source regeneration failed");
            const char *restored = language_join(run, directory, "restored.goat");
            language_write(run, restored, printed.out, printed.out_size);
            CHECK(COMMAND("--lang", "en", "--optimize", optimization, restored), expected, size);
            if (state->index)
                language_remove(run, copied);
            CHECK(COMMAND("--lang", "en", "--run", "--native", "off", binary), expected, size);
        }
        count++;
        printf("[ ok ] %s%s\n", name, rejected ? " (rejected)" : "");
    }
    scenario_require(run, count != 0, "no matching language fixtures");
    printf("%s checks passed: %zu fixtures\n", state->index ? "Index" : "For-loop", count);
}

static void field(scenario_run_t *run, const char *report, const char *key, const char *expected) {
    size_t n = strlen(key), wanted = strlen(expected), found = 0;
    for (const char *p = report; *p;) {
        const char *end = strchr(p, '\n');
        if (!end)
            end = p + strlen(p);
        const char *value_end = end;
        if (value_end > p && value_end[-1] == '\r')
            value_end--;
        if ((size_t)(value_end - p) > n && !strncmp(p, key, n) && p[n] == '=') {
            found++;
            scenario_require(run,
                             (size_t)(value_end - p - n - 1) == wanted
                                 && !memcmp(p + n + 1, expected, wanted),
                             "unexpected persisted-native report field");
        }
        p = *end ? end + 1 : end;
    }
    scenario_require(run, found == 1, "missing or duplicate persisted-native report field");
}

static void native_loops(scenario_run_t *run, state_t *state) {
    const char *work = scenario_work_directory(scenario_run_context(run));
    const char *data = language_join(run,
                                     scenario_data_directory(scenario_run_context(run)),
                                     "functional/native_modes");
    const char *const *environment = NULL;
    const char *infinite = language_join(run, work, "infinite.goat");
    const char *infinite_code = "for (;;) ; println(\"unreachable\");\n";
    language_write(run, infinite, infinite_code, strlen(infinite_code));
    CHECK(COMMAND("--lang", "en", "--compile", infinite), "", 0);
    puts("[ ok ] compile an infinite loop without executing it");
    for (size_t i = 0; i < 2; ++i) {
        const char *directory = language_join(run, work, i ? "long-native" : "native");
        language_directory(run, directory);
        const char *source = language_join(run, directory, "program.goat");
        size_t size;
        const char *code =
            language_read(run,
                          language_join(run, data, i ? "long_loops.goat" : "loops.goat"),
                          &size);
        language_write(run, source, code, size);
        environment = NULL;
        CHECK(COMMAND("--lang", "en", "--compile", "--native", "required", source), "", 0);
        language_remove(run, source);
        const char *expected;
        if (i) {
            expected = "499999500000\n500000500000\n750000\n";
            size = strlen(expected);
        } else
            expected = language_read(run, language_join(run, data, "loops.out"), &size);
        const char *report_path = language_join(run, directory, "native.report");
        environment = language_environment(run, true);
        CHECK(COMMAND("--lang",
                      "en",
                      "--run",
                      "--native",
                      "required",
                      "--save-native",
                      report_path,
                      language_join(run, directory, "program.gbin")),
              expected,
              size);
        const char *report = language_read(run, report_path, &size);
        field(run, report, "preparation", "ready");
        field(run, report, "attempts", i ? "3" : "12");
        field(run, report, "succeeded", i ? "3" : "12");
        field(run, report, "retries", "0");
        puts(i ? "[ ok ] million-iteration loops and shallow call trees remain native"
               : "[ ok ] persisted native loops, all entries succeed without VM retry");
    }
}

static void access_errors(scenario_run_t *run, state_t *state) {
    static const char *invalid[] = {"public private x = 1;",
                                    "public public x = 1;",
                                    "var public x = 1;",
                                    "const private x = 1;",
                                    "public;",
                                    "public x;",
                                    "var obj = {}; public obj.x = 1;",
                                    "var x = public 1;",
                                    "public println(1);"};
    static const char *denied[] = {"const obj={private x=42;}; obj.x;",
                                   "const obj={private var x=42;}; obj[\"x\"]+=1;",
                                   "const obj={protected x=42;}; try obj.x; catch(e) println(e);"};
    const char *const *environment = language_environment(run, true);
    const char *work = scenario_work_directory(scenario_run_context(run));
    for (size_t lang = 0; lang < 2; ++lang) {
        const char *language = lang ? "ru" : "en";
        for (size_t category = 0; category < 2; ++category) {
            size_t count =
                category ? sizeof(denied) / sizeof(*denied) : sizeof(invalid) / sizeof(*invalid);
            for (size_t mode = 0; mode < (category ? 2u : 1u); ++mode) {
                for (size_t i = 0; i < count; ++i) {
                    char name[80];
                    snprintf(name,
                             sizeof(name),
                             "%s-access-%s-%zu-%zu",
                             category ? "denied" : "invalid",
                             language,
                             mode,
                             i);
                    const char *directory = language_join(run, work, name);
                    language_directory(run, directory);
                    const char *source = language_join(run, directory, "program.goat");
                    const char *code = category ? denied[i] : invalid[i];
                    language_write(run, source, code, strlen(code));
                    scenario_command_result_t result =
                        category ? COMMAND("--lang",
                                           language,
                                           "--compile",
                                           "--optimize",
                                           mode ? "all" : "none",
                                           source)
                                 : COMMAND("--lang", language, "--compile", source);
                    scenario_require(run,
                                     result.exit_code != 0 && result.err_size,
                                     "invalid access accepted or silent failure");
                    if (category) {
                        /* Capture buffers are bytes; make a terminated copy for substring checks.
                         */
                        char *error = language_owned(run, ALLOC(result.err_size + 1));
                        memcpy(error, result.err, result.err_size);
                        error[result.err_size] = 0;
                        scenario_require(
                            run,
                            !result.out_size
                                && strstr(error, lang ? "Доступ запрещён" : "Access denied")
                                && strstr(error, lang ? "объявление" : "declaration at")
                                && !strstr(error, "0:0"),
                            "missing localized access declaration diagnostic");
                        scenario_require(
                            run,
                            !language_exists(run, language_join(run, directory, "program.gbin")),
                            "access violation published bytecode");
                    }
                }
            }
        }
    }
}

/** @brief Every counted legacy native entry reads its own immutable actual payload. */
static void native_argument_scalars(scenario_run_t *run, state_t *state) {
    const char *work = scenario_work_directory(scenario_run_context(run));
    const char *code = "const zero=func(){return arguments.length;};\n"
                       "const count=func(n){return arguments.length;};\n"
                       "const read=func(n){n=n+1;return arguments[0]+arguments.length;};\n"
                       "const caller=func(n){return read(n,n+1,\"x\")+arguments[0];};\n"
                       "println(zero());println(zero(null,4));\n"
                       "println(count());println(count(7,8,9));println(count(7,null));\n"
                       "println(count(7,{}));println(read(4,5,null));println(caller(4));\n"
                       "const text=func(n){n=\"changed\";return arguments[0];};\n"
                       "const boolean=func(n){n=!n;return arguments[0];};\n"
                       "const character=func(n){n='z';return arguments[0];};\n"
                       "const real=func(n){n=n+1.0;return arguments[0];};\n"
                       "println(text(\"original\"));println(boolean(true));\n"
                       "println(character('x'));println(real(2.5));\n"
                       "const missing=func(n){arguments[0];return 99;};\n"
                       "const beyond=func(n){arguments[9];return 99;};\n"
                       "const invalid=func(n){arguments[null];return 99;};\n"
                       "try{missing();}catch(e){println(e); }\n"
                       "try{beyond(1);}catch(e){println(e); }\n"
                       "try{invalid(1);}catch(e){println(e); }\n";
    const char *expected = "0\n2\n0\n3\n2\n2\n7\n11\noriginal\ntrue\nx\n2.5\n"
                           "INVALID_ARGUMENT\nINVALID_ARGUMENT\nINVALID_ARGUMENT\n";
    for (size_t mode = 0; mode < 2; mode++) {
        const char *directory = language_join(run, work, mode ? "arguments-all" : "arguments-none");
        language_directory(run, directory);
        const char *source = language_join(run, directory, "program.goat");
        const char *binary = language_join(run, directory, "program.gbin");
        const char *report_path = language_join(run, directory, "native.report");
        const char *optimization = mode ? "all" : "none";
        const char *const *environment = NULL;
        language_write(run, source, code, strlen(code));
        CHECK(COMMAND("--lang", "en", "--native", "off", "--optimize", optimization, source),
              expected,
              strlen(expected));
        /* Native execution requires full optimization; retain the unoptimized VM matrix. */
        if (!mode) {
            CHECK(COMMAND("--lang",
                          "en",
                          "--compile",
                          "--native",
                          "off",
                          "--optimize",
                          optimization,
                          source),
                  "",
                  0);
            language_remove(run, source);
            environment = language_environment(run, true);
            CHECK(COMMAND("--lang", "en", "--run", "--native", "off", binary),
                  expected,
                  strlen(expected));
            continue;
        }
        CHECK(COMMAND("--lang",
                      "en",
                      "--native",
                      "required",
                      "--optimize",
                      optimization,
                      "--save-native",
                      report_path,
                      source),
              expected,
              strlen(expected));
        size_t size;
        const char *report = language_read(run, report_path, &size);
        field(run, report, "attempts", "10");
        field(run, report, "succeeded", "10");
        field(run, report, "retries", "0");
        CHECK(COMMAND("--lang",
                      "en",
                      "--compile",
                      "--native",
                      "required",
                      "--optimize",
                      optimization,
                      source),
              "",
              0);
        language_remove(run, source);
        environment = language_environment(run, true);
        CHECK(COMMAND("--lang", "en", "--run", "--native", "off", binary),
              expected,
              strlen(expected));
        CHECK(COMMAND("--lang",
                      "en",
                      "--run",
                      "--native",
                      "required",
                      "--save-native",
                      report_path,
                      binary),
              expected,
              strlen(expected));
        report = language_read(run, report_path, &size);
        field(run, report, "preparation", "ready");
        field(run, report, "attempts", "10");
        field(run, report, "succeeded", "10");
        field(run, report, "retries", "0");
    }
    puts(
        "[ ok ] native arguments length and original slots preserve reassignment and nested calls");
}

/** @brief Native arguments owners preserve actual arity; boxed escapes use shared VM storage. */
static void native_arguments(scenario_run_t *run, state_t *state) {
    const char *work = scenario_work_directory(scenario_run_context(run));
    const char *directory = language_join(run, work, "native-arguments");
    language_directory(run, directory);
    const char *source = language_join(run, directory, "program.goat");
    const char *binary = language_join(run, directory, "program.gbin");
    const char *report_path = language_join(run, directory, "native.report");
    const char *library = language_join(run, directory, "library.goat");
    const char *descriptor = language_join(run, directory, "library.gmod");
    const char *modules = language_join(run, directory, "modules");
    const char *module_code =
        "external zero = func() { return arguments.length; };\n"
        "external count = func(n) { return arguments.length; };\n"
        "external change = func(n) { n = n + 1; return n + arguments.length; };\n"
        "external nested = func(n) { return count(n,n+1,null) + arguments.length; };\n"
        "external snapshot = func(x,y) { x = 99; return arguments; };\n"
        "external capture = func(x) { const saved=arguments; "
        "return func(y) { println(arguments); return saved; }; };\n"
        /* Only arguments-owning functions seed native specializations. */
        "zero(); count(1); change(1); nested(1);\n";
    const char *module_descriptor = "{\"formatVersion\":1,\"name\":\"arguments-test\","
                                    "\"version\":\"1\",\"main\":\"library.goat\"}";
    const char *code = "const lib = import \"arguments-test@1\";\n"
                       "println(lib.zero()); println(lib.zero(4,null));\n"
                       "println(lib.count()); println(lib.count(7,8,9));\n"
                       "println(lib.count(7,null)); println(lib.count(7,{}));\n"
                       "println(lib.change(4,5)); println(lib.nested(4,5));\n"
                       "var trace=0; const tick=func(n){trace=trace*10+n;return n;};\n"
                       "println(lib.count(tick(1),tick(2),tick(3))); println(trace);\n"
                       "const saved=lib.snapshot(4,5,6); const escaped=lib.capture(8,9);\n"
                       "for(var i=0;i<3000;i++){var trash={var cycle=null;};trash.cycle=trash;}\n"
                       "println(saved); println(escaped(20,21,22)); println(escaped());\n"
                       "try { saved[0]=10; } catch(e) { println(e); }\n"
                       "try { saved.push(10); } catch(e) { println(e); }\n"
                       "println(saved); println(lib.snapshot());\n";
    const char *expected = "0\n2\n0\n3\n2\n2\n7\n5\n3\n123\n"
                           "[4,5,6]\n[20,21,22]\n[8,9]\n[ ]\n[8,9]\n"
                           "IMMUTABLE_OBJECT\nIMMUTABLE_OBJECT\n[4,5,6]\n[ ]\n";
    const char *const *environment = NULL;
    language_write(run, descriptor, module_descriptor, strlen(module_descriptor));
    static const char *invalid[] = {"var arguments = 1;", "if (false) println(arguments);"};
    static const char *errors[] = {
        "Fatal error in 'library.goat', 1.1: Built-in constant 'arguments' cannot be assigned or "
        "redeclared\n",
        "Error in 'library.goat', 1.20: Built-in 'arguments' is only available inside a "
        "function\n"};
    for (size_t i = 0; i < 2; i++) {
        language_write(run, library, invalid[i], strlen(invalid[i]));
        scenario_command_result_t result = COMMAND("--lang",
                                                   "en",
                                                   "build",
                                                   descriptor,
                                                   "--native",
                                                   "off",
                                                   "--module-dir",
                                                   modules);
        language_check(run, result, 1, "", 0, errors[i], strlen(errors[i]));
        scenario_require(
            run,
            !language_exists(run, language_join(run, modules, "arguments-test/1.gbin")),
            "invalid arguments module published bytecode");
    }
    language_write(run, library, module_code, strlen(module_code));
    CHECK(COMMAND("--lang", "en", "build", descriptor, "--native", "auto", "--module-dir", modules),
          "",
          0);
    /* Both optimizer modes execute source and serialized images with the same module. */
    for (size_t mode = 0; mode < 2; mode++) {
        const char *optimization = mode ? "all" : "none";
        environment = NULL;
        language_write(run, source, code, strlen(code));
        CHECK(COMMAND("--lang",
                      "en",
                      "--native",
                      "off",
                      "--optimize",
                      optimization,
                      "--module-dir",
                      modules,
                      source),
              expected,
              strlen(expected));
        /* Native execution requires full optimization; retain the unoptimized VM matrix. */
        if (!mode) {
            CHECK(COMMAND("--lang",
                          "en",
                          "--compile",
                          "--native",
                          "off",
                          "--optimize",
                          optimization,
                          "--module-dir",
                          modules,
                          source),
                  "",
                  0);
            language_remove(run, source);
            language_remove(run, library);
            environment = language_environment(run, true);
            CHECK(COMMAND("--lang",
                          "en",
                          "--run",
                          "--native",
                          "off",
                          "--module-dir",
                          modules,
                          binary),
                  expected,
                  strlen(expected));
            language_remove(run, binary);
            language_write(run, library, module_code, strlen(module_code));
            continue;
        }
        CHECK(COMMAND("--lang",
                      "en",
                      "--native",
                      "auto",
                      "--optimize",
                      optimization,
                      "--save-native",
                      report_path,
                      "--module-dir",
                      modules,
                      source),
              expected,
              strlen(expected));
        size_t size;
        const char *report = language_read(run, report_path, &size);
        field(run, report, "attempts", "10");
        field(run, report, "succeeded", "10");
        field(run, report, "retries", "0");
        CHECK(COMMAND("--lang",
                      "en",
                      "--compile",
                      "--native",
                      "auto",
                      "--optimize",
                      optimization,
                      "--module-dir",
                      modules,
                      source),
              "",
              0);
        language_remove(run, source);
        /* The module source is absent on every persisted run, and no compiler is available. */
        language_remove(run, library);
        environment = language_environment(run, true);
        CHECK(COMMAND("--lang", "en", "--run", "--native", "off", "--module-dir", modules, binary),
              expected,
              strlen(expected));
        CHECK(COMMAND("--lang",
                      "en",
                      "--run",
                      "--native",
                      "auto",
                      "--save-native",
                      report_path,
                      "--module-dir",
                      modules,
                      binary),
              expected,
              strlen(expected));
        report = language_read(run, report_path, &size);
        field(run, report, "preparation", "ready");
        field(run, report, "attempts", "10");
        field(run, report, "succeeded", "10");
        field(run, report, "retries", "0");
        language_write(run, library, module_code, strlen(module_code));
    }
    language_remove(run, library);
    language_remove(run, descriptor);
    puts("[ ok ] native arguments owners preserve arity without retry; boxed escapes survive GC");
}

static void body(scenario_run_t *run, void *opaque) {
    state_t *state = opaque;
    fixtures(run, state);
    if (state->index) {
        access_errors(run, state);
        native_arguments(run, state);
        native_argument_scalars(run, state);
    } else
        native_loops(run, state);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t arguments_error = {0};
    if (!fs_arguments(&argc, &argv, &arguments_error)) {
        clear_fs_error(&arguments_error);
        return 1;
    }
#endif
    int result = 1;
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        puts("native.for\nnative.index");
        result = 0;
    } else if (argc == 5 || argc == 6) {
        fs_error_t error = {0};
        char *exe = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        if (exe) {
            state_t states[] = {{exe, false}, {exe, true}};
            scenario_definition_t definitions[] = {
                {"native.for", 180000, body, &states[0], NULL},
                {"native.index", 180000, body, &states[1], NULL}};
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 6 ? argv[5] : NULL};
            result = scenario_registry_main(definitions, 2, argc == 6 ? 2 : 1, selection, &options);
            FREE(exe);
        }
    } else
        fputs("usage: loop_index_testing native.for|native.index GOAT BUILD DATA [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
