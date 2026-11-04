/** @file package_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Relocated release acceptance, independent of Python (#391).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})
#ifdef _WIN32
#    define EXE ".exe"
#    define LIB ".dll"
#else
#    define EXE ""
#    define LIB ".so"
#endif
typedef struct {
    const char *archive, *delivery, *cc, *source, *cmake, *payload;
} config_t;

typedef struct {
    child_options_t options;
    scenario_command_result_t result;
} command_t;

static void command_body(scenario_run_t *r, void *p) {
    command_t *c = p;
    c->result = scenario_command_capture(r, &c->options);
    REQUIRE(c->result.exit_code == 0);
}

static scenario_command_result_t command(scenario_run_t *r,
                                         const char *exe,
                                         const char *const *args,
                                         const char *cwd,
                                         const char *const *env,
                                         unsigned seconds) {
    size_t count = 0;
    while (args[count])
        count++;
    const char **arguments = language_owned(r, CALLOC((count + 2) * sizeof(*arguments)));
    arguments[0] = exe;
    for (size_t i = 0; i < count; i++)
        arguments[i + 1] = args[i];
    command_t c = {.options = {.executable = exe,
                               .arguments = arguments,
                               .working_directory = cwd,
                               .environment = env,
                               .empty_stdin = true}};
    scenario_with_timeout(r, seconds * 1000, command_body, &c);
    return c.result;
}

static const char *join(scenario_run_t *r, const char *a, const char *b) {
    return language_join(r, a, b);
}

static bool release_entries(void *p) {
    clear_fs_entries(p);
    FREE(p);
    return true;
}

static fs_entries_t *entries(scenario_run_t *r, const char *p) {
    fs_entries_t *e = CALLOC(sizeof(*e));
    scenario_defer(r, release_entries, e);
    fs_error_t error = {0};
    bool ok = list_fs_directory(p, e, &error);
    clear_fs_error(&error);
    REQUIRE(ok);
    return e;
}

static fs_kind_t kind(scenario_run_t *r, const char *p) {
    fs_error_t e = {0};
    fs_info_t i;
    bool ok = inspect_fs_path(p, false, &i, &e);
    clear_fs_error(&e);
    REQUIRE(ok);
    return i.kind;
}

static void move(scenario_run_t *r, const char *a, const char *b) {
    fs_error_t e = {0};
    bool ok = rename_fs_path(a, b, &e);
    clear_fs_error(&e);
    REQUIRE(ok);
}

static void copy(scenario_run_t *r, const char *a, const char *b) {
    size_t n;
    const char *d = language_read(r, a, &n);
    language_write(r, b, d, n);
}

static void equal(scenario_run_t *r, const char *a, const char *b) {
    size_t n, m;
    const char *x = language_read(r, a, &n), *y = language_read(r, b, &m);
    scenario_require_bytes(r, "SDK bytes", x, n, y, m, false);
}

static bool env_key(const char *v, const char *key) {
#ifdef _WIN32
    return !_strnicmp(v, key, strlen(key));
#else
    return !strncmp(v, key, strlen(key));
#endif
}

static const char *const *environment(scenario_run_t *r, bool isolated, const char *compiler) {
    const char *const *base = language_environment_override(
        r,
        "CC",
        isolated ? join(r, scenario_work_directory(scenario_run_context(r)), "no compiler")
                 : compiler);
    size_t n = 0;
    while (base[n])
        n++;
    const char **out = language_owned(r, CALLOC((n + 2) * sizeof(*out)));
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        const char *v = base[i];
        if (env_key(v, "CPATH=") || env_key(v, "C_INCLUDE_PATH=") || env_key(v, "LIBRARY_PATH=")
            || env_key(v, "LD_LIBRARY_PATH=") || (isolated && env_key(v, "PATH=")))
            continue;
        out[k++] = v;
    }
    if (isolated) {
#ifdef _WIN32
        const char *root = getenv("SystemRoot");
        REQUIRE(root);
        const char *p = join(r, root, "System32");
        char *v = language_owned(r, ALLOC(strlen(p) + 6));
        sprintf(v, "PATH=%s", p);
        out[k++] = v;
#else
        out[k++] = "PATH=";
#endif
    }
    return out;
}

static void output(scenario_run_t *r, scenario_command_result_t c, const char *expected) {
    size_t n = c.out_size;
    const char *p = c.out;
    while (n && isspace((unsigned char)*p)) {
        p++;
        n--;
    }
    while (n && isspace((unsigned char)p[n - 1]))
        n--;
    scenario_require_bytes(r, "stdout", expected, strlen(expected), p, n, true);
}

static bool field(const char *s, const char *value) {
    size_t length = strlen(value);
    const char *found = strstr(s, value);
    return found && (found == s || found[-1] == '\n')
           && (found[length] == '\n' || !found[length]
               || (found[length] == '\r' && found[length + 1] == '\n'));
}

static void report(scenario_run_t *r, const char *p, const char *success) {
    size_t n;
    const char *s = language_read(r, p, &n);
    REQUIRE(field(s, success));
    REQUIRE(field(s, "retries=0"));
}

static bool safe_name(const char *s, size_t n) {
    if (!n || s[0] == '/' || s[0] == '\\')
        return false;
    for (size_t i = 0; i < n; i++)
        if (s[i] == '\\' || s[i] == ':' || s[i] == '\r' || (unsigned char)s[i] < 32)
            return false;
    for (size_t i = 0; i < n;) {
        size_t j = i;
        while (j < n && s[j] != '/')
            j++;
        if ((j - i == 2 && !memcmp(s + i, "..", 2)) || (j - i == 1 && s[i] == '.'))
            return false;
        i = j + 1;
    }
    return true;
}

static void tree(scenario_run_t *r, const char *p) {
    fs_entries_t *e = entries(r, p);
    for (size_t i = 0; i < e->count; i++) {
        const char *q = join(r, p, e->names[i]);
        fs_kind_t k = kind(r, q);
        REQUIRE(k == FS_FILE || k == FS_DIRECTORY);
        if (k == FS_DIRECTORY)
            tree(r, q);
    }
}

static bool archive_safe(scenario_run_t *r, config_t *c, const char *path, const char *root) {
    scenario_command_result_t listing =
        command(r, c->cmake, ARGS("-E", "tar", "tf", path), root, NULL, 90);
    const char *s = listing.out, *first = NULL;
    size_t start = 0, first_size = 0;
    for (size_t i = 0; i <= listing.out_size; i++)
        if (i == listing.out_size || s[i] == '\n') {
            if (i > start) {
                size_t n = i - start - (s[i - 1] == '\r' ? 1 : 0), top = 0;
                if (!safe_name(s + start, n))
                    return false;
                while (top < n && s[start + top] != '/')
                    top++;
                if (!first) {
                    first = s + start;
                    first_size = top;
                } else if (top != first_size || memcmp(first, s + start, top))
                    return false;
            }
            start = i + 1;
        }
    if (!first)
        return false;
    listing = command(r, c->cmake, ARGS("-E", "tar", "tvf", path), root, NULL, 90);
    s = listing.out;
    start = 0;
    for (size_t i = 0; i <= listing.out_size; i++)
        if (i == listing.out_size || s[i] == '\n') {
            if (i > start && s[start] != '-' && s[start] != 'd')
                return false;
            start = i + 1;
        }
    return true;
}

static void archive(scenario_run_t *r, config_t *c, const char *path, unsigned index) {
    const char *base = c->payload;
    char name[64];
    snprintf(name, sizeof(name), "package %u", index);
    const char *root = join(r, base, name);
    language_directory(r, root);
    const char *unpacked = join(r, root, "unpacked");
    language_directory(r, unpacked);
    REQUIRE(archive_safe(r, c, path, root));
    command(r, c->cmake, ARGS("-E", "tar", "xf", path), unpacked, NULL, 90);
    tree(r, unpacked);
    fs_entries_t *roots = entries(r, unpacked);
    REQUIRE(roots->count == 1);
    const char *old = join(r, unpacked, roots->names[0]);
    REQUIRE(kind(r, old) == FS_DIRECTORY);
    const char *package = join(r, root, "relocated package");
    move(r, old, package);
    language_remove(r, unpacked);
    const char *required[] = {"include/goat/native_abi.h",
                              "include/goat/native_runtime.h",
                              "include/goat/native_strings.h",
                              "lib/libgoat_native.a",
                              "README.md",
                              "LICENSE.txt",
                              "build-info.txt",
                              "docs/native-memory.md",
                              NULL};
    for (size_t i = 0; required[i]; i++)
        REQUIRE(kind(r, join(r, package, required[i])) == FS_FILE);
    const char *goat = join(r, package, "bin/goat" EXE),
               *inspect = join(r, package, "bin/goat-inspect" EXE),
               *merge = join(r, package, "bin/goat-merge" EXE);
    REQUIRE(kind(r, goat) == FS_FILE);
    REQUIRE(kind(r, inspect) == FS_FILE);
    REQUIRE(kind(r, merge) == FS_FILE);
    const char *work = join(r, root, "user programs");
    language_directory(r, work);
    const char *const *env = environment(r, false, c->cc), *const *isolated =
                                                               environment(r, true, c->cc);
    scenario_command_result_t help = command(r, inspect, ARGS("--help"), work, env, 15);
    REQUIRE(strstr(help.out, "goat-inspect"));
#define RUN(ENV, ...) command(r, goat, ARGS("--lang", "en", __VA_ARGS__), work, ENV, 90)
    RUN(isolated, "--native", "off", join(r, package, "examples/hello_world.goat"));
    const char *rep = join(r, work, "identity.report");
    const char *examples[] = {"native_string_identity.goat",
                              "native_string_concat.goat",
                              "native_quadratic.goat"};
    for (size_t i = 0; i < 3; i++) {
        output(r,
               RUN(env,
                   "--native",
                   "required",
                   "--save-native",
                   rep,
                   join(r, join(r, package, "examples"), examples[i])),
               i < 2 ? "Привет, native strings! 😀"
                     : "Нет корней\nОдин корень: 1.0\nДва корня: 1.0, 2.0");
        report(r, rep, i < 2 ? "succeeded=1" : "succeeded=3");
    }
    const char *source = join(r, work, "numeric.goat"), *binary = join(r, work, "numeric.gbin"),
               *saved = join(r, work, "saved source");
    copy(r, join(r, package, "examples/numeric.goat"), source);
    output(r, RUN(isolated, "--native", "off", source), "55");
    output(r, RUN(isolated, "--compile", "--optimize", "none", source), "");
    move(r, source, saved);
    output(r, RUN(isolated, "--run", "--native", "off", binary), "55");
    move(r, saved, source);
    const char *generated = join(r, work, "printed.c");
    scenario_command_result_t printed = RUN(env, "--print-c", source);
    language_write(r, generated, printed.out, printed.out_size);
    command(r,
            c->cc,
            ARGS("-std=c11",
                 "-O2",
                 "-fno-fast-math",
                 "-ffp-contract=off",
                 "-I",
                 join(r, package, "include/goat"),
                 "-c",
                 generated,
                 "-o",
                 join(r, work, "printed.o")),
            work,
            env,
            90);
    const char *probe_source = join(r, work, "sdk.c"), *probe = join(r, work, "sdk.exe");
    copy(r, join(r, c->source, "test/data/package/sdk.c.txt"), probe_source);
    command(r,
            c->cc,
            ARGS("-std=c11",
                 "-O2",
                 "-I",
                 join(r, package, "include"),
                 probe_source,
                 join(r, package, "lib/libgoat_native.a"),
                 "-o",
                 probe,
#ifdef _WIN32
                 "-static",
                 "-static-libgcc",
#endif
                 NULL),
            work,
            env,
            90);
    language_remove(r, probe_source);
    command(r, probe, ARGS(NULL), work, isolated, 30);
    output(r, RUN(env, "--save-c", source), "");
    equal(r, join(r, work, "libgoat_native.a"), join(r, package, "lib/libgoat_native.a"));
    language_remove(r, join(r, work, "libgoat_native.a"));
    const char *headers[] = {"native_abi.h", "native_runtime.h", "native_strings.h"};
    for (size_t i = 0; i < 3; i++) {
        const char *p = join(r, work, headers[i]);
        equal(r, p, join(r, join(r, package, "include/goat"), headers[i]));
        language_remove(r, p);
    }
    language_remove(r, join(r, work, "numeric.c"));
    language_remove(r, generated);
    output(r, RUN(env, "--compile", "--native", "required", source), "");
    const char *library = join(r, work, "numeric" LIB);
    REQUIRE(kind(r, library) == FS_FILE);
    language_remove(r, source);
    const char *deployed = join(r, root, "deployed program");
    language_directory(r, deployed);
    move(r, binary, join(r, deployed, "numeric.gbin"));
    move(r, library, join(r, deployed, "numeric" LIB));
    output(r,
           RUN(isolated,
               "--run",
               "--native",
               "required",
               "--save-native",
               rep,
               join(r, deployed, "numeric.gbin")),
           "55");
    size_t n;
    const char *text = language_read(r, rep, &n);
    REQUIRE(strstr(text, "preparation=ready"));
    const char *succeeded = strstr(text, "succeeded=");
    REQUIRE(succeeded && succeeded[10] >= '1' && succeeded[10] <= '9');
    command(r,
            c->delivery,
            ARGS("integration.module_delivery.end-to-end",
                 goat,
                 inspect,
                 merge,
                 c->cc,
                 base,
                 join(r, c->source, "test/data"),
                 join(r, c->source, "docs/module-format-v1"),
                 "--timeout-seconds",
                 "300"),
            work,
            env,
            300);
#undef RUN
}

static int compare(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static bool suffix(const char *p, const char *s) {
    size_t n = strlen(p), m = strlen(s);
    return n >= m && !strcmp(p + n - m, s);
}

static void body(scenario_run_t *r, void *p) {
    config_t *c = p;
    c->cc = language_find_executable(r, c->cc);
    if (kind(r, c->archive) == FS_DIRECTORY) {
        fs_entries_t *e = entries(r, c->archive);
        qsort(e->names, e->count, sizeof(*e->names), compare);
        unsigned count = 0;
        for (int type = 0; type < 2; type++)
            for (size_t i = 0; i < e->count; i++)
                if (suffix(e->names[i], type ? ".tar.gz" : ".zip"))
                    archive(r, c, join(r, c->archive, e->names[i]), count++);
        REQUIRE(count > 0);
    } else
        archive(r, c, c->archive, 0);
}

/* Minimal ustar fixtures exercise the actual listing/extraction gate without Python. */
static void tar_header(unsigned char *h, const char *name, char type) {
    memset(h, 0, 512);
    strcpy((char *)h, name);
    memcpy(h + 100, "0000755", 7);
    memcpy(h + 108, "0000000", 7);
    memcpy(h + 116, "0000000", 7);
    memcpy(h + 124, "00000000000", 11);
    memcpy(h + 136, "00000000000", 11);
    memset(h + 148, ' ', 8);
    h[156] = (unsigned char)type;
    if (type == '2')
        strcpy((char *)h + 157, "../../escape");
    memcpy(h + 257, "ustar", 5);
    memcpy(h + 263, "00", 2);
    unsigned sum = 0;
    for (size_t i = 0; i < 512; i++)
        sum += h[i];
    snprintf((char *)h + 148, 7, "%06o", sum);
    h[155] = ' ';
}

static void controls(scenario_run_t *r, void *p) {
    config_t *c = p;
    REQUIRE(field("succeeded=1\nretries=0\n", "succeeded=1"));
    REQUIRE(!field("succeeded=10\n", "succeeded=1"));
    REQUIRE(!field("retries=01\n", "retries=0"));
    REQUIRE(!field("xsucceeded=1\n", "succeeded=1"));
    const char *root = scenario_work_directory(scenario_run_context(r));
    const char *names[] = {"root/file", "other/file", "../escape", "root/link"};
    for (unsigned i = 0; i < 4; i++) {
        unsigned char bytes[2048] = {0};
        tar_header(bytes, "root/", '5');
        tar_header(bytes + 512, names[i], i == 3 ? '2' : '0');
        char name[32];
        snprintf(name, sizeof(name), "control-%u.tar", i);
        const char *path = join(r, root, name);
        language_write(r, path, bytes, sizeof(bytes));
        REQUIRE(archive_safe(r, c, path, root) == (i == 0));
    }
}

static const char *last_separator(const char *path) {
    const char *a = strrchr(path, '/');
#ifdef _WIN32
    const char *b = strrchr(path, '\\');
    if (!a || (b && b > a))
        a = b;
#endif
    return a;
}

/* Retention runs after scenario cleanup, so it cannot use fail-fast scenario APIs. */
static bool retain_tree(const char *source, const char *destination, fs_error_t *error) {
    fs_info_t info;
    if (!inspect_fs_path(source, false, &info, error))
        return false;
    if (info.kind == FS_DIRECTORY) {
        if (!create_fs_directory(destination, error))
            return false;
        fs_entries_t entries = {0};
        if (!list_fs_directory(source, &entries, error))
            return false;
        bool ok = true;
        for (size_t i = 0; i < entries.count && ok; i++) {
            char *a = join_fs_path(source, entries.names[i], error);
            char *b = join_fs_path(destination, entries.names[i], error);
            ok = a && b && retain_tree(a, b, error);
            FREE(a);
            FREE(b);
        }
        clear_fs_entries(&entries);
        return ok;
    }
    if (info.kind != FS_FILE)
        return false;
    FILE *in = FOPEN(source, "rb"), *out = in ? FOPEN(destination, "wbx") : NULL;
    bool ok = in && out;
    unsigned char buffer[65536];
    while (ok) {
        size_t n = FREAD(buffer, 1, sizeof(buffer), in);
        if (n && !write_fs_file(out, buffer, n, destination, error)) {
            ok = false;
            break;
        }
        if (n < sizeof(buffer)) {
            ok = !ferror(in);
            break;
        }
    }
    if (in && FCLOSE(in))
        ok = false;
    if (out && !close_fs_file(out, destination, error))
        ok = false;
    return ok;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t arguments_error = {0};
    if (!fs_arguments(&argc, &argv, &arguments_error)) {
        clear_fs_error(&arguments_error);
        return 1;
    }
    clear_fs_error(&arguments_error);
#endif
    int result = 1;
    bool keep = false;
    unsigned timeout = 1800;
    bool valid = argc >= 7;
    for (int i = 7; i < argc && valid; i++) {
        if (!strcmp(argv[i], "--keep-work"))
            keep = true;
        else if (!strcmp(argv[i], "--timeout-seconds") && i + 1 < argc) {
            char *end;
            unsigned long n = strtoul(argv[++i], &end, 10);
            valid = *argv[i] && !*end && n > 0 && n <= 86400;
            timeout = (unsigned)n;
        } else
            valid = false;
    }
    if (valid) {
        char *paths[5] = {0};
        for (int i = 0; i < 5; i++) {
            fs_error_t e = {0};
            if (i == 2) {
                paths[i] = ALLOC(strlen(argv[i + 2]) + 1);
                strcpy(paths[i], argv[i + 2]);
            } else
                paths[i] = absolute_fs_path(argv[i + 2], &e);
            clear_fs_error(&e);
            valid = valid && paths[i];
        }
        if (valid) {
            config_t c = {paths[0], paths[1], paths[2], paths[3], paths[4], NULL};
            scenario_definition_t defs[] = {
                {"integration.package.end-to-end", (uint64_t)timeout * 1000, body, &c, NULL},
                {"integration.package.controls", 120000, controls, &c, NULL}};
            fs_error_t error = {0};
            char *temporary = temporary_fs_directory(&error);
            char *prefix =
                temporary ? join_fs_path(temporary, "goat package acceptance", &error) : NULL;
            char *workspace = prefix ? create_fs_temporary(prefix, true, NULL, &error) : NULL;
            if (!workspace)
                fprintf(stderr,
                        "Cannot create package payload workspace: %s (status %d, native %lu), path "
                        "%s\n",
                        error.operation ? error.operation : "allocation failed",
                        (int)error.status,
                        error.native_code,
                        error.path ? error.path : "unavailable");
            clear_fs_error(&error);
            if (workspace) {
                char *build = ALLOC(strlen(paths[1]) + 1);
                strcpy(build, paths[1]);
                char *slash = (char *)last_separator(build);
                if (slash) {
                    if (slash == build || (slash == build + 2 && build[1] == ':'))
                        slash[1] = 0;
                    else
                        *slash = 0;
                }
                char *data = join_fs_path(paths[3], "test/data", &error);
                if (!data)
                    fprintf(
                        stderr,
                        "Cannot resolve package test data: %s (status %d, native %lu), source %s\n",
                        error.operation ? error.operation : "allocation failed",
                        (int)error.status,
                        error.native_code,
                        paths[3]);
                clear_fs_error(&error);
                c.payload = workspace;
                scenario_options_t options = {.build_directory = build, .data_directory = data};
                const char *selection[] = {argv[1], "--keep-work"};
                result = scenario_registry_main(defs, 2, keep ? 2 : 1, selection, &options);
                if (result || keep) {
                    char *diagnostics = join_fs_path(build, "package-diagnostics", &error);
                    fs_info_t info;
                    bool found = diagnostics && inspect_fs_path(diagnostics, false, &info, &error);
                    bool ready = found && info.kind == FS_DIRECTORY;
                    if (!found && error.status == FS_NOT_FOUND) {
                        clear_fs_error(&error);
                        ready = diagnostics && create_fs_directory(diagnostics, &error);
                    }
                    const char *name = last_separator(workspace);
                    char *destination =
                        ready ? join_fs_path(diagnostics, name ? name + 1 : workspace, &error)
                              : NULL;
                    if (!destination || !retain_tree(workspace, destination, &error)) {
                        fprintf(stderr,
                                "Package diagnostics copy failed (status %d, native %lu; existing "
                                "target must be a real directory); original retained at %s\n",
                                (int)error.status,
                                error.native_code,
                                workspace);
                        result = 1;
                    } else
                        fprintf(stderr, "Package diagnostics retained at %s\n", destination);
                    FREE(destination);
                    FREE(diagnostics);
                } else if (!remove_fs_tree(workspace, &error))
                    result = 1;
                clear_fs_error(&error);
                FREE(data);
                FREE(build);
            }
            FREE(workspace);
            FREE(prefix);
            FREE(temporary);
        }
        for (int i = 0; i < 5; i++)
            FREE(paths[i]);
    } else
        fputs("usage: package_scenarios ID ARCHIVE_OR_DIR DELIVERY CC SOURCE_ROOT CMAKE "
              "[--keep-work] [--timeout-seconds N]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
