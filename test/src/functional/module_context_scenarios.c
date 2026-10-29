/** @file module_context_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Launch-bound constants, canonical paths and synchronized retargets (#360).
 */
#ifndef _WIN32
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/module_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <unistd.h>
#endif
#define REQUIRE(x) scenario_require(r, (x), #x)
#define N(x) (sizeof(x) / sizeof((x)[0]))

typedef struct {
    const char *host, *goat, *compiler_source;
    unsigned test;
    bool skipped;
} configuration_t;

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool free_malloc(void *p) {
    free(p);
    return true;
}

static const char *text(scenario_run_t *r, const char *format, ...) {
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int n = vsnprintf(NULL, 0, format, args);
    va_end(args);
    REQUIRE(n >= 0);
    char *out = language_owned(r, ALLOC((size_t)n + 1));
    vsnprintf(out, (size_t)n + 1, format, copy);
    va_end(copy);
    return out;
}

static const char *source(scenario_run_t *r, yyjson_val *data, const char *key) {
    yyjson_val *v = yyjson_obj_get(data, key);
    const char *s = yyjson_get_str(v);
    REQUIRE(s && strlen(s) == yyjson_get_len(v));
    return s;
}

static const char *canonical(scenario_run_t *r, const char *path) {
    fs_error_t e = {0};
    char *p = canonical_fs_path(path, &e);
    clear_fs_error(&e);
    REQUIRE(p != NULL);
    return language_owned(r, p);
}

static const char *directory(scenario_run_t *r, const char *path) {
    char *p = (char *)canonical(r, path);
    char *slash = strrchr(p, '/');
    REQUIRE(slash != NULL);
    *slash = 0;
    return p;
}

static const char *compile(scenario_run_t *r,
                           module_execution_project_t *p,
                           yyjson_val *data,
                           const char *name,
                           const char *key) {
    return module_execution_compile(p, name, source(r, data, key), NULL, NULL, 0, NULL, false);
}

static void binary(module_execution_project_t *p, const char *file, const char *expected) {
    module_execution_invoke(p, "binary", file, NULL, expected, strlen(expected), false);
}

static void run(module_execution_project_t *p, const char *code, const char *expected) {
    module_execution_run(p, code, expected, strlen(expected));
}

static bool
link_file(scenario_run_t *r, configuration_t *c, const char *target, const char *alias) {
#ifdef _WIN32
    int nt = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, target, -1, NULL, 0);
    int na = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, alias, -1, NULL, 0);
    REQUIRE(nt > 0 && na > 0);
    wchar_t *t = language_owned(r, ALLOC((size_t)nt * sizeof(*t)));
    wchar_t *a = language_owned(r, ALLOC((size_t)na * sizeof(*a)));
    REQUIRE(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, target, -1, t, nt) > 0);
    REQUIRE(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, alias, -1, a, na) > 0);
    bool ok = CreateSymbolicLinkW(a, t, 2) != 0;
    if (!ok && GetLastError() == ERROR_INVALID_PARAMETER)
        ok = CreateSymbolicLinkW(a, t, 0) != 0;
    if (!ok && GetLastError() == ERROR_PRIVILEGE_NOT_HELD) {
        c->skipped = true;
        puts("SKIP: Windows symlink privilege not held");
        return false;
    }
#else
    (void)c;
    bool ok = symlink(target, alias) == 0;
#endif
    REQUIRE(ok);
    return true;
}

static bool contains(const unsigned char *p, size_t n, const unsigned char *needle, size_t size) {
    if (size > n)
        return false;
    for (size_t i = 0; i <= n - size; i++)
        if (!memcmp(p + i, needle, size))
            return true;
    return false;
}

/* Independent UTF-8 -> UTF-32LE encoding for the path-leak oracle. */
static const unsigned char *utf32(scenario_run_t *r, const char *s, size_t *size) {
    unsigned char *out = language_owned(r, ALLOC(4 * strlen(s) + 1));
    *size = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p;) {
        uint32_t v = *p++;
        unsigned count = 0;
        if (v >= 0xf0) {
            v &= 7;
            count = 3;
        } else if (v >= 0xe0) {
            v &= 15;
            count = 2;
        } else if (v >= 0xc0) {
            v &= 31;
            count = 1;
        } else
            REQUIRE(v < 128);
        while (count--) {
            REQUIRE((*p & 0xc0) == 0x80);
            v = (v << 6) | (*p++ & 63);
        }
        for (unsigned j = 0; j < 4; j++)
            out[(*size)++] = (unsigned char)(v >> (8 * j));
    }
    return out;
}

static void no_paths(scenario_run_t *r, const char *archive, const char *root) {
    size_t size;
    const unsigned char *bytes = (const unsigned char *)language_read(r, archive, &size);
    format_zip_info_t info;
    char error[256];
    REQUIRE(format_zip_inspect(bytes, size, SIZE_MAX, &info, error, sizeof(error)));
    /* Both native Windows separators and slash spelling, as in the Python oracle. */
    char *native = (char *)text(r, "%s", root);
#ifdef _WIN32
    for (char *p = native; *p; p++)
        if (*p == '/')
            *p = '\\';
#endif
    const char *spellings[] = {root, native};
    for (size_t i = 0; i < info.count; i++) {
        size_t n;
        unsigned char *entry =
            format_zip_extract(bytes, size, i, 16 * 1024 * 1024, &n, error, sizeof(error));
        REQUIRE(entry != NULL);
        scenario_defer(r, free_malloc, entry);
        for (size_t j = 0; j < N(spellings); j++) {
            REQUIRE(!contains(entry, n, (const unsigned char *)spellings[j], strlen(spellings[j])));
            size_t encoded_size;
            const unsigned char *encoded = utf32(r, spellings[j], &encoded_size);
            REQUIRE(!contains(entry, n, encoded, encoded_size));
        }
    }
}

typedef struct {
    configuration_t *c;
    const char *alias, *replacement;
} retarget_t;

static void retarget(scenario_run_t *r, void *arg) {
    retarget_t *a = arg;
    if (a->alias) {
        language_remove(r, a->alias);
        REQUIRE(link_file(r, a->c, a->replacement, a->alias));
    }
}

static void check(scenario_run_t *r, scenario_command_result_t result, const char *expected) {
    REQUIRE(result.exit_code == 0);
    scenario_require_bytes(r,
                           "context stdout",
                           expected,
                           strlen(expected),
                           result.out,
                           result.out_size,
                           true);
}

static const char *const *compiler_environment(scenario_run_t *r,
                                               const char *compiler,
                                               const char *alias,
                                               const char *swapped,
                                               const char *marker) {
    const char *const *base = language_environment_override(r, "CC", compiler);
    size_t n = 0;
    while (base[n])
        n++;
    const char **env = language_owned(r, CALLOC((n + 4) * sizeof(*env)));
    size_t out = 0;
    const char *names[] = {"GOAT_TEST_ALIAS=", "GOAT_TEST_SWAPPED=", "GOAT_TEST_MARKER="};
    for (size_t i = 0; i < n; i++) {
        bool keep = true;
        for (size_t j = 0; j < N(names); j++) {
#ifdef _WIN32
            if (!_strnicmp(base[i], names[j], strlen(names[j])))
                keep = false;
#else
            if (!strncmp(base[i], names[j], strlen(names[j])))
                keep = false;
#endif
        }
        if (keep)
            env[out++] = base[i];
    }
    env[out++] = text(r, "%s%s", names[0], alias);
    env[out++] = text(r, "%s%s", names[1], swapped);
    env[out] = text(r, "%s%s", names[2], marker);
    return env;
}

static void body(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *data_root = scenario_data_directory(scenario_run_context(r));
    size_t n;
    const char *json =
        language_read(r, language_join(r, data_root, "module-context/sources.json"), &n);
    yyjson_doc *doc = yyjson_read(json, n, 0);
    REQUIRE(doc != NULL);
    scenario_defer(r, free_doc, doc);
    yyjson_val *data = yyjson_doc_get_root(doc);
    REQUIRE(yyjson_is_obj(data));
    module_execution_suite_t *suite = module_execution_suite(r, c->host);
    const char *name = c->test == 4 ? "каталог-λ-🦉" : "project";
    module_execution_project_t *p = module_execution_project(suite, name);
    const char *root = module_execution_root(p);
    if (c->test == 0) {
        const char *lib = compile(r, p, data, "library", "same-library");
        size_t before_n;
        const char *before = language_read(r, lib, &before_n);
        binary(p, lib, text(r, "direct\n%s\n", directory(r, lib)));
        const char *dir = canonical(r, root);
        run(p,
            source(r, data, "same-app"),
            text(r, "imported\n%s\ntrue\nfalse\ntrue\n%s\n", dir, dir));
        size_t after_n;
        const char *after = language_read(r, lib, &after_n);
        scenario_require_bytes(r, "unchanged library", before, before_n, after, after_n, false);
    } else if (c->test == 1) {
        module_execution_file_t file = {"nested/other.goat", source(r, data, "origin-other")};
        const char *app = module_execution_compile(p,
                                                   "app",
                                                   source(r, data, "origin-app"),
                                                   NULL,
                                                   &file,
                                                   1,
                                                   NULL,
                                                   false);
        binary(p, app, source(r, data, "origin-expected"));
    } else if (c->test == 2) {
        compile(r, p, data, "library", "reexport-library");
        compile(r, p, data, "facade", "reexport-facade");
        run(p, source(r, data, "reexport-app"), source(r, data, "reexport-expected"));
    } else if (c->test == 3 || c->test == 4) {
        const char *archive = compile(r, p, data, "app", "launch-app");
        if (c->test == 3)
            no_paths(r, archive, root);
        binary(p, archive, text(r, "true\n%s\n", directory(r, archive)));
        if (c->test == 3) {
            const char *moved_dir =
                language_join(r, scenario_work_directory(scenario_run_context(r)), "moved");
            language_directory(r, moved_dir);
            const char *moved = language_join(r, moved_dir, "app.gbin");
            fs_error_t e = {0};
            bool ok = rename_fs_path(archive, moved, &e);
            clear_fs_error(&e);
            REQUIRE(ok);
            ok = remove_fs_tree(language_join(r, root, "sources"), &e);
            clear_fs_error(&e);
            REQUIRE(ok);
            binary(p, moved, text(r, "true\n%s\n", directory(r, moved)));
        }
    } else if (c->test >= 5 && c->test <= 8) {
        compile(r, p, data, "library", "canonical-library");
        const char *archive = compile(r, p, data, "app", "canonical-app");
        const char *expected = text(r, "42\n%s\ntrue\n", directory(r, archive));
        module_execution_project_t *links = module_execution_project(suite, "links");
        module_execution_put(links, "modules/library/1.gbin", "invalid alias-side dependency");
        const char *alias = language_join(r, module_execution_root(links), "app.gbin");
        bool symbolic = c->test == 6 || c->test == 8;
        if (symbolic && !link_file(r, c, archive, alias))
            return;
        const char *launch = symbolic ? alias : archive;
        if (c->test < 7)
            binary(p, launch, expected);
        else {
            module_execution_project_t *other = module_execution_project(suite, "replacement");
            const char *replacement = compile(r, other, data, "app", "replacement-app");
            retarget_t action = {c, symbolic ? alias : NULL, replacement};
            const char *args[] = {c->host,
                                  "frozen-chdir",
                                  launch,
                                  module_execution_root(other),
                                  NULL};
            child_options_t options = {.executable = c->host,
                                       .arguments = args,
                                       .working_directory = module_execution_unrelated(suite),
                                       .environment = module_execution_environment(p)};
            check(r,
                  scenario_command_barrier(r,
                                           &options,
#ifdef _WIN32
                                           "READY\r\n",
#else
                                           "READY\n",
#endif
                                           retarget,
                                           &action),
                  text(r, "READY\n%s", expected));
        }
    } else if (c->test == 9 || c->test == 10) {
        module_execution_project_t *unicode = module_execution_project(suite, "исходник-λ-🦉");
        const char *file =
            module_execution_put(unicode, "main.goat", source(r, data, "cli-source"));
        const char *expected = text(r, "true\n1\n%s\n1\n", directory(r, file));
        const char *alias = language_join(r, root, "main.goat");
        if (c->test == 10 && !link_file(r, c, file, alias))
            return;
        const char *optimizers[] = {"none", "all", "all"}, *native[] = {"off", "off", "auto"};
        for (size_t i = 0; i < N(native); i++) {
            const char *args[] = {c->goat,
                                  "--optimize",
                                  optimizers[i],
                                  "--native",
                                  native[i],
                                  c->test == 10 ? alias : file,
                                  NULL};
            child_options_t options = {.executable = c->goat,
                                       .arguments = args,
                                       .working_directory = module_execution_unrelated(suite)};
            check(r, scenario_command_capture(r, &options), expected);
        }
    } else if (c->test == 11) {
        const char *original =
            module_execution_put(p, "before-native/main.goat", source(r, data, "native-source"));
        const char *replacement = module_execution_put(p,
                                                       "after-native/main.goat",
                                                       source(r, data, "replacement-source"));
        const char *alias = language_join(r, root, "native.goat"),
                   *swapped = language_join(r, root, "swapped.goat");
        if (!link_file(r, c, original, alias))
            return;
        REQUIRE(link_file(r, c, replacement, swapped));
#ifdef _WIN32
        const char *compiler = language_join(r, root, "compiler.exe");
#else
        const char *compiler = language_join(r, root, "compiler");
#endif
        const char *cc = getenv("CC");
        if (!cc || !*cc)
            cc = "gcc";
        const char *build[] = {cc,
                               "-std=c11",
                               "-Wall",
                               "-Wextra",
                               "-Werror",
                               c->compiler_source,
                               "-o",
                               compiler,
                               NULL};
        child_options_t build_options = {.executable = cc, .arguments = build};
        REQUIRE(scenario_command_capture(r, &build_options).exit_code == 0);
        const char *marker = language_join(r, root, "compiler-called");
        const char *args[] = {c->goat, "--optimize", "all", "--native", "auto", alias, NULL};
        child_options_t options = {.executable = c->goat,
                                   .arguments = args,
                                   .working_directory = module_execution_unrelated(suite),
                                   .environment =
                                       compiler_environment(r, compiler, alias, swapped, marker)};
        check(r,
              scenario_command_capture(r, &options),
              text(r, "8\ntrue\n%s\n", directory(r, original)));
        size_t marker_n;
        const char *marker_text = language_read(r, marker, &marker_n);
        scenario_require_bytes(r,
                               "compiler called",
                               "retargeted",
                               10,
                               marker_text,
                               marker_n,
                               false);
        fs_error_t e = {0};
        fs_info_t info;
        bool ok = inspect_fs_path(alias, false, &info, &e);
        clear_fs_error(&e);
        REQUIRE(ok && info.kind == FS_LINK);
        REQUIRE(!strcmp(canonical(r, alias), canonical(r, replacement)));
    } else {
        const char *formats[] = {"%s = 1;",
                                 "%s += 1;",
                                 "var %s = 1;",
                                 "const %s = 1;",
                                 "public %s = 1;",
                                 "external %s = 1;",
                                 "const f = func() { var %s = 1; };",
                                 "const f = func(%s) { return 1; };",
                                 "const f = func() { %s = 1; };",
                                 "if (false) { const %s = 1; };",
                                 "try { throw 1; } catch (%s) { };",
                                 "for (const %s in [1]) { };"};
        unsigned index = c->test - 12;
        REQUIRE(index < 24);
        module_execution_compile(p,
                                 "app",
                                 text(r, formats[index % 12], index < 12 ? "__main__" : "__dir__"),
                                 NULL,
                                 NULL,
                                 0,
                                 NULL,
                                 true);
    }
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t ae = {0};
    if (!fs_arguments(&argc, &argv, &ae)) {
        clear_fs_error(&ae);
        return 1;
    }
#endif
    const char *names[] = {"same-image",
                           "file-origin",
                           "reexport",
                           "relocation",
                           "unicode",
                           "canonical-direct",
                           "canonical-alias",
                           "frozen-direct",
                           "frozen-alias",
                           "source-direct",
                           "source-alias",
                           "native-alias"};
    int result = 1;
    if (argc == 7 || argc == 8) {
        fs_error_t e = {0};
        char *host = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        char *goat = absolute_fs_path(argv[3], &e);
        clear_fs_error(&e);
        char *compiler = absolute_fs_path(argv[6], &e);
        clear_fs_error(&e);
        if (host && goat && compiler) {
            char ids[36][128];
            configuration_t config[36];
            scenario_definition_t defs[36];
            for (unsigned i = 0; i < 36; i++) {
                if (i < N(names))
                    snprintf(ids[i],
                             sizeof(ids[i]),
                             "integration.module_context_constants.%s",
                             names[i]);
                else
                    snprintf(ids[i],
                             sizeof(ids[i]),
                             "integration.module_context_constants.reserved-%s-%u",
                             i < 24 ? "main" : "dir",
                             (i - 12) % 12);
                config[i] = (configuration_t){host, goat, compiler, i, false};
                defs[i] = (scenario_definition_t){ids[i], 120000, body, &config[i], NULL};
            }
            scenario_options_t options = {.build_directory = argv[4], .data_directory = argv[5]};
            const char *selection[] = {argv[1], argc == 8 ? argv[7] : NULL};
            result = scenario_registry_main(defs, N(defs), argc == 8 ? 2 : 1, selection, &options);
            for (unsigned i = 0; i < N(config); i++)
                if (!result && config[i].skipped)
                    result = 77;
        }
        FREE(host);
        FREE(goat);
        FREE(compiler);
    } else
        fputs("usage: module_context_scenarios ID HOST GOAT BUILD DATA COMPILER_SOURCE "
              "[--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
