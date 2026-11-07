/** @file native_integrity_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent SHA binding and pre-load rejection observations (#380).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#include "test/support/language_execution.h"
#include "test/support/module_artifact.h"
#include "test/support/registry.h"

#include <string.h>
#ifdef _WIN32
#    define EXT ".dll"
#else
#    define EXT ".so"
#endif
#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})

typedef struct {
    const char *goat, *fixtures, *kind;
} config_t;

typedef struct {
    const unsigned char *data;
    size_t size;
} bytes_t;

typedef struct {
    child_options_t options;
    scenario_command_result_t result;
} command_t;

static bytes_t read(scenario_run_t *r, const char *path) {
    size_t n;
    const char *data = language_read(r, path, &n);
    return (bytes_t){(const unsigned char *)data, n};
}

static void put(scenario_run_t *r, const char *path, bytes_t bytes) {
    language_write(r, path, bytes.data, bytes.size);
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

static bytes_t read_text(scenario_run_t *r, const char *path) {
    bytes_t input = read(r, path);
    REQUIRE(utf8(input.data, input.size));
    unsigned char *normalized = language_owned(r, ALLOC(input.size + 1));
    size_t n = 0;
    for (size_t i = 0; i < input.size; i++) {
        if (input.data[i] == '\r') {
            normalized[n++] = '\n';
            if (i + 1 < input.size && input.data[i + 1] == '\n')
                i++;
        } else
            normalized[n++] = input.data[i];
    }
    return (bytes_t){normalized, n};
}

static void write_text(scenario_run_t *r, const char *path, bytes_t bytes) {
#ifdef _WIN32
    unsigned char *expanded = language_owned(r, ALLOC(2 * bytes.size + 1));
    size_t n = 0;
    for (size_t i = 0; i < bytes.size; i++) {
        if (bytes.data[i] == '\n')
            expanded[n++] = '\r';
        expanded[n++] = bytes.data[i];
    }
    put(r, path, (bytes_t){expanded, n});
#else
    put(r, path, bytes);
#endif
}

static void invoke(scenario_run_t *r, void *user) {
    command_t *c = user;
    c->result = scenario_command_capture(r, &c->options);
}

static scenario_command_result_t
command(scenario_run_t *r, const char *const *args, const char *const *env, uint64_t timeout) {
    command_t c = {.options = {.executable = args[0],
                               .arguments = args,
                               .environment = env,
                               .empty_stdin = true}};
    scenario_with_timeout(r, timeout, invoke, &c);
    if (timeout == 30000) {
        REQUIRE(utf8(c.result.out, c.result.out_size));
        REQUIRE(utf8(c.result.err, c.result.err_size));
    }
    return c.result;
}

static void output(scenario_run_t *r, scenario_command_result_t result, const char *expected) {
    const unsigned char *raw = result.out;
    unsigned char *normalized = language_owned(r, ALLOC(result.out_size + 1));
    size_t used = 0;
    for (size_t i = 0; i < result.out_size; i++) {
        if (raw[i] == '\r') {
            normalized[used++] = '\n';
            if (i + 1 < result.out_size && raw[i + 1] == '\n')
                i++;
        } else
            normalized[used++] = raw[i];
    }
    scenario_require_bytes(r, "native stdout", expected, strlen(expected), normalized, used, false);
}

static const char *const *
marker_environment(scenario_run_t *r, const char *const *base, const char *path) {
    const char key[] = "GOAT_INTEGRITY_MARKER=";
    size_t n = 0, used = 0;
    while (base[n])
        n++;
    const char **out = language_owned(r, CALLOC((n + 2) * sizeof(*out)));
    for (size_t i = 0; i < n; i++) {
#ifdef _WIN32
        bool match = !_strnicmp(base[i], key, sizeof(key) - 1);
#else
        bool match = !strncmp(base[i], key, sizeof(key) - 1);
#endif
        if (!match)
            out[used++] = base[i];
    }
    char *entry = language_owned(r, ALLOC(sizeof(key) + strlen(path)));
    strcpy(entry, key);
    strcat(entry, path);
    out[used] = entry;
    return out;
}

static bool exists(scenario_run_t *r, const char *path) {
    fs_info_t info;
    fs_error_t error = {0};
    bool found = inspect_fs_path(path, true, &info, &error);
    bool missing = error.status == FS_NOT_FOUND;
    clear_fs_error(&error);
    REQUIRE(found || missing);
    return found;
}

static void digest(bytes_t bytes, unsigned char hash[32]) {
    char hex[65];
    format_binary_sha256(bytes.data, bytes.size, hex);
    const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; i++)
        hash[i] = (unsigned char)((strchr(digits, hex[2 * i]) - digits) * 16
                                  + (strchr(digits, hex[2 * i + 1]) - digits));
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
    const char *root = scenario_work_directory(scenario_run_context(r));
    const char *source = language_join(r, root, "original.goat");
    const char *binary = language_join(r, root, "original.gbin");
    const char *member = "native/variant-1" EXT;
    const char *other = language_join(r, root, "other.goat");
    const char *const *env = language_environment_override(r, "GOAT_LANGUAGE", "en");
    write_text(r, source, read_text(r, language_join(r, c->fixtures, "original.goat")));
    scenario_command_result_t result =
        command(r, ARGS(c->goat, "--compile", "--native", "required", source), env, 30000);
    REQUIRE(result.exit_code == 0);
    bytes_t artifact = read(r, binary);
    size_t native_size;
    const void *native_bytes =
        test_module_member(r, artifact.data, artifact.size, member, &native_size);
    bytes_t original = {native_bytes, native_size};
    size_t metadata_size;
    const char *metadata = (const char *)
        test_module_member(r, artifact.data, artifact.size, "module.json", &metadata_size);
    char expected[65];
    format_binary_sha256(original.data, original.size, expected);
    bool hash_found = false;
    for (size_t i = 0; i + 64 <= metadata_size; ++i)
        if (!memcmp(metadata + i, expected, 64))
            hash_found = true;
    REQUIRE(hash_found);
    output(r,
           command(r, ARGS(c->goat, "--run", "--native", "required", binary), env, 30000),
           "42\n");
    if (!strcmp(c->kind, "sha256-binding"))
        return;
    write_text(r, other, read_text(r, language_join(r, c->fixtures, "other.goat")));
    result = command(r, ARGS(c->goat, "--compile", "--native", "required", other), env, 30000);
    REQUIRE(result.exit_code == 0);
    if (!strcmp(c->kind, "substitution") || !strcmp(c->kind, "append-corruption")) {
        bytes_t changed;
        if (!strcmp(c->kind, "substitution")) {
            bytes_t other_zip = read(r, language_join(r, root, "other.gbin"));
            changed.data =
                test_module_member(r, other_zip.data, other_zip.size, member, &changed.size);
        } else {
            unsigned char *bytes = language_owned(r, ALLOC(original.size + 7));
            memcpy(bytes, original.data, original.size);
            memcpy(bytes + original.size, "changed", 7);
            changed = (bytes_t){bytes, original.size + 7};
        }
        size_t size;
        const void *zip = test_module_replace(r,
                                              artifact.data,
                                              artifact.size,
                                              member,
                                              changed.data,
                                              changed.size,
                                              false,
                                              &size);
        put(r, binary, (bytes_t){zip, size});
        result = command(r, ARGS(c->goat, "--run", "--native", "required", binary), env, 30000);
        REQUIRE(result.exit_code != 0 && result.out_size == 0);
        result = command(r, ARGS(c->goat, "--run", "--native", "auto", binary), env, 30000);
        REQUIRE(result.exit_code != 0 && result.out_size == 0);
        result = command(r, ARGS(c->goat, "--run", "--native", "off", binary), env, 30000);
        REQUIRE(result.exit_code != 0 && result.out_size == 0);
        return;
    }
    REQUIRE(!strcmp(c->kind, "preload-constructor"));
    result = command(r, ARGS(c->goat, "--save-c", other), env, 30000);
    REQUIRE(result.exit_code == 0);
    const char *generated = language_join(r, root, "other.c");
    bytes_t code = read_text(r, generated),
            suffix = read(r, language_join(r, c->fixtures, "constructor.c.inc"));
    unsigned char *combined = language_owned(r, ALLOC(code.size + suffix.size));
    memcpy(combined, code.data, code.size);
    memcpy(combined + code.size, suffix.data, suffix.size);
    write_text(r, generated, (bytes_t){combined, code.size + suffix.size});
    const char *poison = language_join(r, root, "poison" EXT);
    char *raw = fs_environment("CC", NULL);
    const char *cc = raw ? language_owned(r, raw) : "gcc";
    const char *args[] = {cc,
                          "-std=c11",
                          "-O2",
                          "-shared",
                          "-DGOAT_NATIVE_BUILD",
                          generated,
                          "-o",
                          poison,
                          "-lm",
#ifdef _WIN32
                          "-static",
                          "-static-libgcc",
                          "-Wl,--exclude-all-symbols",
#else
                          "-fPIC",
#endif
                          NULL};
    result = command(r, args, NULL, 60000);
    REQUIRE(result.exit_code == 0);
    const char *marker = language_join(r, root, "loaded.txt");
    if (exists(r, marker))
        language_remove(r, marker);
    env = marker_environment(r, env, marker);
    bytes_t poisoned = read(r, poison);
    size_t zip_size;
    const void *zip = test_module_replace(r,
                                          artifact.data,
                                          artifact.size,
                                          member,
                                          poisoned.data,
                                          poisoned.size,
                                          false,
                                          &zip_size);
    put(r, binary, (bytes_t){zip, zip_size});
    result = command(r, ARGS(c->goat, "--run", "--native", "required", binary), env, 30000);
    REQUIRE(result.exit_code != 0 && !exists(r, marker));
    zip = test_module_replace(r,
                              artifact.data,
                              artifact.size,
                              member,
                              poisoned.data,
                              poisoned.size,
                              true,
                              &zip_size);
    put(r, binary, (bytes_t){zip, zip_size});
    result = command(r, ARGS(c->goat, "--run", "--native", "required", binary), env, 30000);
    /* A correctly hashed payload reaches the OS loader (and its constructors),
       then fails module ABI validation. SHA-256 is integrity, not a sandbox. */
    REQUIRE(result.exit_code != 0 && exists(r, marker));
    language_remove(r, marker);
    const char *loader = language_join(r, root, "loader.c");
    const char loader_code[] =
        "#ifdef _WIN32\n#include <windows.h>\n"
        "int main(int n,char **v){return n!=2||!LoadLibraryA(v[1]);}\n#else\n"
        "#include <dlfcn.h>\nint main(int n,char **v){return "
        "n!=2||!dlopen(v[1],RTLD_NOW);}\n#endif\n";
    put(r, loader, (bytes_t){loader_code, sizeof(loader_code) - 1});
    const char *loader_exe = language_join(r, root, "loader.exe");
    result = command(r,
                     ARGS(cc,
                          loader,
                          "-o",
                          loader_exe,
#ifndef _WIN32
                          "-ldl",
#endif
                          "-std=c11"),
                     NULL,
                     60000);
    REQUIRE(result.exit_code == 0);
    result = command(r, ARGS(loader_exe, poison), env, 30000);
    REQUIRE(result.exit_code == 0 && exists(r, marker));
    bytes_t marked = read_text(r, marker);
    scenario_require_bytes(r,
                           "constructor positive control",
                           "loaded",
                           6,
                           marked.data,
                           marked.size,
                           true);
    put(r, binary, artifact);
    output(r,
           command(r, ARGS(c->goat, "--run", "--native", "required", binary), env, 30000),
           "42\n");
}

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
    int result = 1;
    if (argc == 6 || (argc == 7 && scenario_keep_work_argument(argv[6]))) {
        const char *kinds[] = {"sha256-binding",
                               "substitution",
                               "append-corruption",
                               "preload-constructor"};
        config_t configs[4];
        scenario_definition_t definitions[4];
        char ids[4][96];
        fs_error_t error = {0};
        char *goat = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        if (goat) {
            for (size_t i = 0; i < 4; i++) {
                snprintf(ids[i], sizeof(ids[i]), "native.native_integrity.%s", kinds[i]);
                configs[i] = (config_t){goat, argv[5], kinds[i]};
                definitions[i] = (scenario_definition_t){ids[i], 360000, body, configs + i, NULL};
            }
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(definitions, 4, argc == 7 ? 2 : 1, selection, &options);
        }
        FREE(goat);
    } else
        fputs("usage: native_integrity_scenarios ID GOAT BUILD DATA FIXTURES [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
