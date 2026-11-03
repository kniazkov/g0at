/** @file native_runtime_strings_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Frozen runtime string inputs, VM parity, SDK boundaries and ownership (#385).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdarg.h>
#include <string.h>

#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})

typedef struct {
    const char *goat, *repo, *fixtures, *kind;
    const char *const *flags;
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

static bytes_t read(scenario_run_t *r, const char *path) {
    size_t n;
    const char *p = language_read(r, path, &n);
    return (bytes_t){(const unsigned char *)p, n};
}

/* Frozen inputs are the original UTF-8/LF writes. Path.write_text translates LF
 * to CRLF on Windows, including newlines in the generated C and Goat sources. */
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

static const char *fixture(scenario_run_t *r, config_t *c, const char *name, const char *target) {
    const char *path = language_join(r, scenario_work_directory(scenario_run_context(r)), target);
    write_text(r, path, read(r, language_join(r, c->fixtures, name)));
    return path;
}

static const char *work_path(scenario_run_t *r, const char *name) {
    return language_join(r, scenario_work_directory(scenario_run_context(r)), name);
}

static const char *repository(scenario_run_t *r, config_t *c, const char *name) {
    return language_join(r, c->repo, name);
}

static const char *include(scenario_run_t *r, config_t *c, const char *name) {
    return string(r, "-I%s", *name ? repository(r, c, name) : c->repo);
}

static const char *build_file(scenario_run_t *r, config_t *c, const char *name) {
    const char *slash = strrchr(c->goat, '/');
    REQUIRE(slash);
    return string(r, "%.*s/%s", (int)(slash - c->goat), c->goat, name);
}

static const char *environment(scenario_run_t *r, const char *name, const char *fallback) {
    char *value = fs_environment(name, NULL);
    return value ? language_owned(r, value) : fallback;
}

static void command(scenario_run_t *r, void *user) {
    command_t *c = user;
    c->result = scenario_command_capture(r, &c->options);
}

/* Python uses binary capture here, not text=True: neither UTF-8 decoding nor
 * universal newline translation is allowed for captured stdout/stderr. */
static scenario_command_result_t
run(scenario_run_t *r, config_t *c, const char *const *args, bool runtime) {
    command_t call = {.options = {.executable = args[0],
                                  .arguments = args,
                                  .working_directory = c->repo,
                                  .empty_stdin = true}};
    if (runtime)
        scenario_with_timeout(r, 90000, command, &call);
    else
        command(r, &call);
    return call.result;
}

static size_t arguments_size(const char *const *args) {
    size_t n = 0;
    while (args && args[n])
        n++;
    return n;
}

static const char *const *
arguments(scenario_run_t *r, const char *const *a, const char *const *b, const char *const *c) {
    size_t na = arguments_size(a), nb = arguments_size(b), nc = arguments_size(c);
    const char **out = language_owned(r, CALLOC((na + nb + nc + 1) * sizeof(*out)));
    if (na)
        memcpy(out, a, na * sizeof(*out));
    if (nb)
        memcpy(out + na, b, nb * sizeof(*out));
    if (nc)
        memcpy(out + na + nb, c, nc * sizeof(*out));
    return out;
}

static const char *const *common(scenario_run_t *r, config_t *c) {
    return arguments(r,
                     ARGS(environment(r, "CC", "gcc"),
                          "-std=c11",
                          "-O2",
                          "-Wall",
                          "-Wextra",
                          "-Werror",
                          "-pedantic-errors",
                          include(r, c, "include")),
                     c->flags,
                     NULL);
}

static void compile(scenario_run_t *r, config_t *c, const char *const *tail) {
    REQUIRE(run(r, c, arguments(r, common(r, c), tail, NULL), false).exit_code == 0);
}

static bool contains(bytes_t bytes, const char *needle) {
    size_t n = strlen(needle);
    for (size_t i = 0; n <= bytes.size && i <= bytes.size - n; i++)
        if (!memcmp(bytes.data + i, needle, n))
            return true;
    return false;
}

/* Python bytes.splitlines(), unlike str.splitlines(), recognizes only CR, LF
 * and CRLF. The final line terminator adds no extra line; embedded zeros, VT,
 * FF, Unicode line separators and all other bytes remain ordinary data. */
static bytes_t line(bytes_t input, size_t *offset) {
    size_t start = *offset;
    while (*offset < input.size && input.data[*offset] != '\r' && input.data[*offset] != '\n')
        ++*offset;
    size_t end = *offset;
    if (*offset < input.size) {
        unsigned char separator = input.data[(*offset)++];
        if (separator == '\r' && *offset < input.size && input.data[*offset] == '\n')
            ++*offset;
    }
    return (bytes_t){input.data + start, end - start};
}

static bool same_lines(bytes_t a, bytes_t b) {
    size_t ai = 0, bi = 0;
    while (ai < a.size && bi < b.size) {
        bytes_t al = line(a, &ai), bl = line(b, &bi);
        if (al.size != bl.size || memcmp(al.data, bl.data, al.size))
            return false;
    }
    return ai == a.size && bi == b.size;
}

static bytes_t stdout_bytes(scenario_command_result_t result) {
    return (bytes_t){result.out, result.out_size};
}

static bool output_ok(scenario_command_result_t result, bytes_t reference) {
    return result.exit_code == 0 && same_lines(stdout_bytes(result), reference);
}

static bool invalid_ok(scenario_command_result_t result) {
    return result.exit_code != 0
           && contains((bytes_t){result.err, result.err_size}, "INVALID_ARGUMENT");
}

static void save(scenario_run_t *r, const char *name, bytes_t bytes) {
    language_write(r, work_path(r, name), bytes.data, bytes.size);
}

static void regular(scenario_run_t *r, const char *path) {
    fs_error_t error = {0};
    fs_info_t info;
    bool ok = inspect_fs_path(path, true, &info, &error);
    clear_fs_error(&error);
    REQUIRE(ok && info.kind == FS_FILE);
}

static const char *runtime(scenario_run_t *r, config_t *c) {
    const char *archive = build_file(r, c, "libgoat_native.a");
    regular(r, archive);
    if (!arguments_size(c->flags))
        return archive;
    /* Preserve the original sanitizer path: instrument the algorithms as well
     * as both drivers, with the exact original four sources and AR override. */
    archive = work_path(r, "libgoat_native-instrumented.a");
    const char *sources[] = {"src/native/strings.c",
                             "src/native/str.c",
                             "lib/numbers/number_format.c",
                             "lib/strings/unicode.c"};
    const char *names[] = {"strings-runtime.o",
                           "str-runtime.o",
                           "number_format-runtime.o",
                           "unicode-runtime.o"};
    const char *objects[5] = {0};
    for (size_t i = 0; i < 4; i++) {
        objects[i] = work_path(r, names[i]);
        compile(r,
                c,
                ARGS(include(r, c, ""),
                     include(r, c, "src"),
                     "-fPIC",
                     "-c",
                     repository(r, c, sources[i]),
                     "-o",
                     objects[i]));
    }
    REQUIRE(run(r,
                c,
                arguments(r, ARGS(environment(r, "AR", "ar"), "rcs", archive), objects, NULL),
                false)
                .exit_code
            == 0);
    return archive;
}

static const char *library(scenario_run_t *r, const char *stem) {
#ifdef _WIN32
    return work_path(r, string(r, "%s.dll", stem));
#else
    return work_path(r, string(r, "%s.so", stem));
#endif
}

static void compile_module(scenario_run_t *r,
                           config_t *c,
                           const char *source,
                           const char *archive,
                           const char *output) {
#ifdef _WIN32
    const char *const *shared = ARGS("-shared", "-DGOAT_NATIVE_BUILD", "-static", "-static-libgcc");
#else
    const char *const *shared = ARGS("-shared", "-DGOAT_NATIVE_BUILD", "-fPIC");
#endif
    compile(
        r,
        c,
        arguments(
            r,
            shared,
            ARGS(include(r, c, "test/src/native/runtime_strings"), source, archive, "-o", output),
            NULL));
}

static const char *compile_host(scenario_run_t *r, config_t *c, bool automatic) {
    const char *host = work_path(r, automatic ? "compiler-host.exe" : "host.exe");
#ifdef _WIN32
    const char *compiler_library = "-lbcrypt";
    const char *const *loader_libraries = NULL;
#else
    const char *compiler_library = "-ldl";
    const char *const *loader_libraries = ARGS("-ldl");
#endif
    if (automatic)
        compile(r,
                c,
                ARGS("-DTEST_NATIVE_COMPILER",
                     include(r, c, ""),
                     include(r, c, "src"),
                     repository(r, c, "test/src/native/runtime_strings/host.c"),
                     build_file(r, c, "libcore.a"),
                     build_file(r, c, "libcommon.a"),
                     "-o",
                     host,
                     "-lpthread",
                     "-lm",
                     compiler_library));
    else
        compile(r,
                c,
                arguments(r,
                          ARGS("-DMEMORY_DEBUG",
                               include(r, c, ""),
                               include(r, c, "src"),
                               repository(r, c, "test/src/native/runtime_strings/host.c"),
                               repository(r, c, "src/model/native_services.c"),
                               repository(r, c, "lib/strings/unicode.c"),
                               repository(r, c, "lib/memory/allocate.c"),
                               "-o",
                               host),
                          loader_libraries,
                          NULL));
    return host;
}

static bytes_t reference(scenario_run_t *r, config_t *c, const char *source) {
    scenario_command_result_t result =
        run(r, c, ARGS(c->goat, "--optimize", "none", "--native", "off", source), true);
    save(r, "vm.txt", stdout_bytes(result));
    REQUIRE(result.exit_code == 0);
    return stdout_bytes(result);
}

/* Match Path.glob("automatic.*.goat-*"): any entry kind is forbidden, including
 * an empty directory or broken symlink, and unrelated names remain allowed. */
static bool temporary_name(const char *name) {
#ifdef _WIN32
    if (_strnicmp(name, "automatic.", 10))
        return false;
    for (const char *p = name + 10; *p; p++)
        if (!_strnicmp(p, ".goat-", 6))
            return true;
#else
    if (strncmp(name, "automatic.", 10))
        return false;
    if (strstr(name + 10, ".goat-"))
        return true;
#endif
    return false;
}

static bool cleaned(scenario_run_t *r) {
    fs_entries_t entries = {0};
    fs_error_t error = {0};
    bool ok = list_fs_directory(scenario_work_directory(scenario_run_context(r)), &entries, &error);
    clear_fs_error(&error);
    bool clean = true;
    for (size_t i = 0; i < entries.count; i++)
        if (temporary_name(entries.names[i]))
            clean = false;
    clear_fs_entries(&entries);
    REQUIRE(ok);
    return clean;
}

static bytes_t external(scenario_run_t *r, config_t *c) {
    const char *source = fixture(r, c, "strings.c.txt", "strings.c");
    const char *program = fixture(r, c, "strings.goat", "strings.goat");
    const char *archive = runtime(r, c), *module = library(r, "strings");
    compile_module(r, c, source, archive, module);
    const char *host = compile_host(r, c, false);
    scenario_command_result_t native = run(r, c, ARGS(host, module), true);
    save(r, "native.txt", stdout_bytes(native));
    REQUIRE(native.exit_code == 0);
    bytes_t vm = reference(r, c, program);
    scenario_require(r, output_ok(native, vm), "Native/VM mismatch; see native.txt and vm.txt");
    return vm;
}

static void automatic(scenario_run_t *r, config_t *c, bytes_t vm) {
    REQUIRE(!arguments_size(c->flags));
    const char *source = fixture(r, c, "automatic.c.txt", "automatic.c");
    const char *host = compile_host(r, c, true);
    const char *module = library(r, "automatic");
    scenario_command_result_t result =
        run(r, c, ARGS(host, module, environment(r, "CC", "gcc"), source), true);
    save(r, "automatic.txt", stdout_bytes(result));
    scenario_require(r,
                     output_ok(result, vm),
                     "Automatic/VM mismatch; see automatic.txt and vm.txt");
    scenario_require(r, cleaned(r), "Temporary SDK files were not removed");
}

static scenario_command_result_t invalid(scenario_run_t *r, config_t *c, unsigned index) {
    const char *name = string(r, "invalid-%u.goat", index);
    const char *source = fixture(r, c, name, name);
    scenario_command_result_t result =
        run(r,
            c,
            ARGS(c->goat, "--lang", "en", "--optimize", "none", "--native", "off", source),
            true);
    scenario_require(r,
                     invalid_ok(result),
                     "Invalid string argument must fail with INVALID_ARGUMENT");
    return result;
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

static bytes_t literal(const char *s) {
    return (bytes_t){(const unsigned char *)s, strlen(s)};
}

static void negative(scenario_run_t *r, config_t *c) {
    if (!strcmp(c->kind, "negative-output")) {
        /* Exercise the same predicate with real VM stdout, then corrupt one byte. */
        bytes_t vm = reference(r, c, fixture(r, c, "strings.goat", "strings.goat"));
        REQUIRE(vm.size > 0);
        scenario_command_result_t result = {.exit_code = 0, .out = vm.data, .out_size = vm.size};
        REQUIRE(output_ok(result, vm));
        unsigned char *changed = language_owned(r, ALLOC(vm.size));
        memcpy(changed, vm.data, vm.size);
        changed[0] ^= 1;
        result.out = changed;
        REQUIRE(!output_ok(result, vm));
        result.out = vm.data;
        result.exit_code = 1;
        REQUIRE(!output_ok(result, vm));
        REQUIRE(same_lines(literal("a\r\nb\rc\n"), literal("a\nb\nc")));
        REQUIRE(same_lines(literal(""), literal("")));
        REQUIRE(same_lines(literal("\r\n"), literal("\n")));
        REQUIRE(!same_lines(literal(""), literal("\n")));
        REQUIRE(!same_lines(literal("a\n"), literal("a\n\n")));
        REQUIRE(!same_lines(literal("a\vb"), literal("a\nb")));
        REQUIRE(!same_lines(literal("a\fb"), literal("a\nb")));
        REQUIRE(!same_lines(literal("a\x1c"
                                    "b"),
                            literal("a\nb")));
        REQUIRE(!same_lines(literal("a\xc2\x85"
                                    "b"),
                            literal("a\nb")));
        REQUIRE(!same_lines(literal("a\xe2\x80\xa8"
                                    "b"),
                            literal("a\nb")));
        REQUIRE(same_lines((bytes_t){(const unsigned char *)"a\0b\n", 4},
                           (bytes_t){(const unsigned char *)"a\0b", 3}));
        REQUIRE(!same_lines((bytes_t){(const unsigned char *)"a\0b", 3}, literal("a")));
        REQUIRE(same_lines((bytes_t){(const unsigned char *)"\xff\n", 2},
                           (bytes_t){(const unsigned char *)"\xff", 1}));
    } else if (!strcmp(c->kind, "negative-invalid")) {
        scenario_command_result_t result = invalid(r, c, 0);
        result.exit_code = 0;
        REQUIRE(!invalid_ok(result));
        result.exit_code = 1;
        result.err = "DIFFERENT_ERROR";
        result.err_size = strlen(result.err);
        REQUIRE(!invalid_ok(result));
        result.err = "\0INVALID_ARGUMENT\0";
        result.err_size = 18;
        REQUIRE(invalid_ok(result));
        /* Real successful execution cannot satisfy the invalid-call checker. */
        const char *source = fixture(r, c, "strings.goat", "strings.goat");
        result = run(r,
                     c,
                     ARGS(c->goat, "--lang", "en", "--optimize", "none", "--native", "off", source),
                     true);
        REQUIRE(result.exit_code == 0);
        REQUIRE(!invalid_ok(result));
    } else if (!strcmp(c->kind, "negative-cleanup")) {
        REQUIRE(cleaned(r));
        const char *allowed[] = {"automatic.c",
                                 "automatic.so",
                                 "other.so.goat-leftover",
                                 "automatic.goat-leftover"};
        for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++)
            language_write(r, work_path(r, allowed[i]), "", 0);
        REQUIRE(cleaned(r));
        const char *leftover = work_path(r, "automatic.so.goat-leftover");
        language_write(r, leftover, "", 0);
        REQUIRE(!cleaned(r));
        language_remove(r, leftover);
        REQUIRE(cleaned(r));
        leftover = work_path(r, "automatic..goat-");
        language_directory(r, leftover);
        REQUIRE(!cleaned(r));
        language_remove(r, leftover);
        REQUIRE(cleaned(r));
#ifdef _WIN32
        leftover = work_path(r, "AUTOMATIC.DLL.GOAT-leftover");
        language_write(r, leftover, "", 0);
        REQUIRE(!cleaned(r));
#endif
    } else {
        REQUIRE(!strcmp(c->kind, "negative-host"));
        /* All 281 actual operations and the unchanged boundary/ABI/ownership
         * assertions run first. Only assertion termination becomes normal 86,
         * so a crash, timeout or cleanup failure cannot stand in for rejection. */
        bytes_t fixture_bytes = read(r, language_join(r, c->fixtures, "automatic.c.txt"));
        const char *base = string(r, "%.*s", (int)fixture_bytes.size, fixture_bytes.data);
        base = replace(r, base, "\"native_strings.h\"", "\"goat/native_strings.h\"");
        base =
            replace(r,
                    base,
                    "#include <assert.h>",
                    "#include <stdlib.h>\n#define assert(x) do { if (!(x)) exit(86); } while (0)");
        const char *corrupt = replace(r,
                                      base,
                                      "assert(out.length == 3 &&",
                                      "out.length = 2; assert(out.length == 3 &&");
        const char *archive = runtime(r, c), *host = compile_host(r, c, false);
        for (unsigned i = 0; i < 2; i++) {
            const char *source = work_path(r, i ? "corrupt.c" : "baseline.c");
            write_text(r, source, literal(i ? corrupt : base));
            const char *module = library(r, i ? "corrupt" : "baseline");
            compile_module(r, c, source, archive, module);
            scenario_command_result_t result = run(r, c, ARGS(host, module), true);
            if (!i)
                REQUIRE(result.exit_code == 0);
            else {
                REQUIRE(result.exit_code == 86);
                REQUIRE(!output_ok(result, stdout_bytes(result)));
            }
        }
    }
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
    if (!strcmp(c->kind, "all")) {
        bytes_t vm = external(r, c);
        if (!arguments_size(c->flags))
            automatic(r, c, vm);
        for (unsigned i = 0; i < 7; i++)
            invalid(r, c, i);
    } else if (!strcmp(c->kind, "external-sdk"))
        external(r, c);
    else if (!strcmp(c->kind, "automatic-backend"))
        automatic(r, c, reference(r, c, fixture(r, c, "strings.goat", "strings.goat")));
    else if (!strncmp(c->kind, "invalid-", 8))
        invalid(r, c, (unsigned)(c->kind[8] - '0'));
    else
        negative(r, c);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
    int result = 1, next = 6;
    bool keep = argc > next && scenario_keep_work_argument(argv[next]);
    if (keep)
        next++;
    const char *const *flags = NULL;
    if (argc > next && !strcmp(argv[next], "--"))
        flags = (const char *const *)(argv + ++next);
    else if (argc > next)
        next = -1;
    if (argc >= 6 && next >= 0) {
        static const char *kinds[] = {"external-sdk",
                                      "automatic-backend",
                                      "invalid-0",
                                      "invalid-1",
                                      "invalid-2",
                                      "invalid-3",
                                      "invalid-4",
                                      "invalid-5",
                                      "invalid-6",
                                      "negative-output",
                                      "negative-invalid",
                                      "negative-host",
                                      "negative-cleanup",
                                      "all"};
        static const uint64_t timeouts[] = {300000,
                                            300000,
                                            90000,
                                            90000,
                                            90000,
                                            90000,
                                            90000,
                                            90000,
                                            90000,
                                            90000,
                                            180000,
                                            300000,
                                            10000,
                                            300000};

        enum { COUNT = sizeof(kinds) / sizeof(kinds[0]) };

        config_t configs[COUNT];
        scenario_definition_t defs[COUNT];
        char ids[COUNT][112];
        fs_error_t error = {0};
        char *goat = canonical_fs_path(argv[2], &error);
        clear_fs_error(&error);
        char *repo = canonical_fs_path(argv[3], &error);
        clear_fs_error(&error);
        char *data = absolute_fs_path(argv[5], &error);
        clear_fs_error(&error);
        char *fixtures = data ? join_fs_path(data, "native-runtime-strings", &error) : NULL;
        clear_fs_error(&error);
        if (goat && repo && fixtures) {
            for (size_t i = 0; i < COUNT; i++) {
                snprintf(ids[i], sizeof(ids[i]), "integration.native_runtime_strings.%s", kinds[i]);
                configs[i] = (config_t){goat, repo, fixtures, kinds[i], flags};
                defs[i] = (scenario_definition_t){ids[i], timeouts[i], body, configs + i, NULL};
                if (arguments_size(flags) && !strcmp(kinds[i], "automatic-backend"))
                    defs[i].skip_reason = "Explicit compiler flags rebuild the external runtime; "
                                          "the original omits the automatic backend in this mode";
            }
            scenario_options_t options = {.build_directory = argv[4], .data_directory = argv[5]};
            const char *selection[] = {
                !strcmp(argv[1], "all") ? "integration.native_runtime_strings.all" : argv[1],
                keep ? "--keep-work" : NULL};
            result = scenario_registry_main(defs, COUNT, keep ? 2 : 1, selection, &options);
        }
        FREE(fixtures);
        FREE(data);
        FREE(repo);
        FREE(goat);
    } else
        fputs("usage: native_runtime_strings_scenarios all|ID|--list GOAT REPOSITORY BUILD DATA "
              "[--keep-work] [-- COMPILER_FLAGS...]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
