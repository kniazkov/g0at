/** @file module_validation_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared validation phases, snapshots and nonexecution contracts (#366).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)

typedef struct {
    const char *host, *schema;
    yyjson_val *fixture, *all;
    size_t mode;
} configuration_t;

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool free_mut(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool free_malloc(void *p) {
    free(p);
    return true;
}

static void *owned(scenario_run_t *r, void *p) {
    REQUIRE(p);
    scenario_defer(r, free_malloc, p);
    return p;
}

static const char *field(scenario_run_t *r, yyjson_val *v, const char *key) {
    yyjson_val *s = yyjson_obj_get(v, key);
    REQUIRE(yyjson_is_str(s) && strlen(yyjson_get_str(s)) == yyjson_get_len(s));
    return yyjson_get_str(s);
}

static yyjson_val *fixture(scenario_run_t *r, configuration_t *c, const char *id) {
    for (size_t i = 0; i < yyjson_arr_size(c->all); i++) {
        yyjson_val *v = yyjson_arr_get(c->all, i);
        if (!strcmp(field(r, v, "id"), id))
            return v;
    }
    REQUIRE(false);
    return NULL;
}

static void absent(scenario_run_t *r, const char *path) {
    fs_error_t e = {0};
    fs_info_t info;
    bool found = inspect_fs_path(path, true, &info, &e);
    bool missing = e.status == FS_NOT_FOUND;
    clear_fs_error(&e);
    REQUIRE(!found && missing);
}

static unsigned digit(scenario_run_t *r, char c) {
    if (c >= '0' && c <= '9')
        return (unsigned)(c - '0');
    if (c >= 'a' && c <= 'f')
        return (unsigned)(c - 'a' + 10);
    REQUIRE(false);
    return 0;
}

static unsigned char *input(scenario_run_t *r, yyjson_val *v, size_t *size) {
    const char *hex = field(r, v, "inputHex");
    size_t n = strlen(hex);
    REQUIRE(!(n % 2));
    *size = n / 2;
    unsigned char *data = owned(r, malloc(*size + 1));
    for (size_t i = 0; i < *size; i++)
        data[i] = (unsigned char)(16 * digit(r, hex[2 * i]) + digit(r, hex[2 * i + 1]));
    return data;
}

static yyjson_doc *parse(scenario_run_t *r, const char *data, size_t size) {
    yyjson_doc *d = yyjson_read(data, size, 0);
    REQUIRE(d);
    scenario_defer(r, free_doc, d);
    return d;
}

static int number(scenario_run_t *r, yyjson_val *v, const char *key) {
    yyjson_val *n = yyjson_obj_get(v, key);
    REQUIRE(yyjson_is_int(n));
    return (int)yyjson_get_int(n);
}

/* Check original universal assertions independently of the frozen expected diagnostics.
 * Expected events additionally lock every diagnostic field, ordering and prerequisite.
 * Dynamic file labels and native entry names are substituted, never path-normalized away.
 */
static scenario_command_result_t validate(scenario_run_t *r,
                                          configuration_t *c,
                                          const void *bytes,
                                          size_t size,
                                          const char *role,
                                          const char *label,
                                          const char *limit1,
                                          const char *limit2,
                                          int status,
                                          yyjson_val *expected,
                                          const char *native_name,
                                          const char *const *env) {
    const char *args[] = {c->host, role, label, limit1, limit2, NULL};
    child_options_t options = {.executable = c->host,
                               .arguments = args,
                               .stdin_data = bytes,
                               .stdin_size = size,
                               .environment = env};
    scenario_command_result_t result = scenario_command_capture(r, &options);
    REQUIRE(result.exit_code == 0);
    const char *out = result.out;
    size_t offset = 0, count = 0, failures = 0, skips = 0;
    bool bytecode_pass = false, native_pass = false;
    yyjson_val *last = NULL, *wanted = expected ? yyjson_obj_get(expected, "events") : NULL;
    while (offset < result.out_size) {
        size_t end = offset;
        while (end < result.out_size && out[end] != '\n')
            end++;
        yyjson_val *v = yyjson_doc_get_root(parse(r, out + offset, end - offset));
        offset = end < result.out_size ? end + 1 : end;
        REQUIRE(yyjson_is_obj(v));
        if (yyjson_obj_get(v, "status")) {
            REQUIRE(offset == result.out_size);
            last = v;
            break;
        }
        int state = number(r, v, "state"), check = number(r, v, "check");
        REQUIRE(state >= 0 && state <= 2);
        failures += state == 1;
        skips += state == 2;
        REQUIRE(!strcmp(field(r, v, "archive"), label));
        bytecode_pass |= check == 2 && state == 0;
        native_pass |= check == 3 && state == 0;
        if (wanted) {
            yyjson_val *e = yyjson_arr_get(wanted, count);
            REQUIRE(e);
            size_t i, n;
            yyjson_val *k, *value;
            REQUIRE(yyjson_obj_size(v) == yyjson_obj_size(e));
            yyjson_obj_foreach(e, i, n, k, value) {
                const char *key = yyjson_get_str(k);
                if (!strcmp(key, "archive"))
                    continue;
                if (native_name && !strcmp(key, "entry") && yyjson_is_str(value)
                    && !strcmp(yyjson_get_str(value), "native/main.so"))
                    REQUIRE(!strcmp(field(r, v, "entry"), native_name));
                else
                    REQUIRE(yyjson_equals(yyjson_obj_get(v, key), value));
            }
        }
        count++;
    }
    REQUIRE(last);
    REQUIRE(number(r, last, "status") == status);
    REQUIRE(number(r, last, "events") == (int)count && number(r, last, "failures") == (int)failures
            && number(r, last, "skipped") == (int)skips);
    yyjson_val *metadata = yyjson_obj_get(last, "metadata");
    REQUIRE(metadata);
    if (!yyjson_is_null(metadata))
        REQUIRE(!strcmp(field(r, metadata, "name"), "validation"));
    if (status == 0)
        REQUIRE(!yyjson_is_null(metadata) && !failures && !skips && bytecode_pass && native_pass);
    if (expected) {
        REQUIRE(count == yyjson_arr_size(wanted));
        yyjson_val *er = yyjson_obj_get(expected, "result"), *em = yyjson_obj_get(er, "metadata");
        REQUIRE(yyjson_is_null(metadata) == yyjson_is_null(em));
        if (!native_name)
            REQUIRE(yyjson_equals(metadata, em));
    }
    return result;
}

static void corpus(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    size_t size;
    unsigned char *bytes = input(r, c->fixture, &size);
    char a[32], b[32];
    const char *la = NULL, *lb = NULL;
    yyjson_val *limits = yyjson_obj_get(c->fixture, "limits");
    if (yyjson_is_arr(limits)) {
        REQUIRE(yyjson_arr_size(limits) == 2);
        snprintf(a,
                 sizeof(a),
                 "%llu",
                 (unsigned long long)yyjson_get_uint(yyjson_arr_get(limits, 0)));
        snprintf(b,
                 sizeof(b),
                 "%llu",
                 (unsigned long long)yyjson_get_uint(yyjson_arr_get(limits, 1)));
        la = a;
        lb = b;
    }
    scenario_command_result_t first = validate(r,
                                               c,
                                               bytes,
                                               size,
                                               field(r, c->fixture, "role"),
                                               field(r, c->fixture, "label"),
                                               la,
                                               lb,
                                               number(r, c->fixture, "status"),
                                               c->fixture,
                                               NULL,
                                               NULL);
    /* Keep the original repeated valid/schema diagnostics assertion explicit; repeat all cases. */
    scenario_command_result_t second = validate(r,
                                                c,
                                                bytes,
                                                size,
                                                field(r, c->fixture, "role"),
                                                field(r, c->fixture, "label"),
                                                la,
                                                lb,
                                                number(r, c->fixture, "status"),
                                                c->fixture,
                                                NULL,
                                                NULL);
    scenario_require_bytes(r,
                           "repeat diagnostics",
                           first.out,
                           first.out_size,
                           second.out,
                           second.out_size,
                           true);
}

static void api(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *args[] = {c->host, NULL};
    scenario_command_result_t result = language_command(r, c->host, args, NULL);
    language_check(r, result, 0, "Validation API checks passed\n", 29, NULL, 0);
}

static void file_case(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    yyjson_val *base = fixture(r, c, "integration.module_validation.compiler.valid");
    size_t size;
    unsigned char *bytes = input(r, base, &size);
    const char *path = language_join(r,
                                     scenario_work_directory(scenario_run_context(r)),
                                     c->mode == 3 ? "missing.gbin" : "модуль.gbin");
    if (c->mode != 3)
        language_write(r, path, bytes, size);
    char limit[32];
    snprintf(limit, sizeof(limit), "%zu", size - (c->mode == 2));
    validate(r,
             c,
             "",
             0,
             "file",
             path,
             c->mode == 1 || c->mode == 2 ? limit : NULL,
             c->mode == 1 || c->mode == 2 ? "1000000" : NULL,
             c->mode == 3   ? 2
             : c->mode == 2 ? 3
                            : 0,
             c->mode < 2 ? base : NULL,
             NULL,
             NULL);
    if (c->mode < 2) {
        size_t n;
        const char *replacement = language_read(r, path, &n);
        scenario_require_bytes(r,
                               "file replaced after snapshot",
                               "replacement",
                               11,
                               replacement,
                               n,
                               false);
    } else if (c->mode == 2) {
        size_t n;
        const char *original = language_read(r, path, &n);
        scenario_require_bytes(r, "limit leaves input unchanged", bytes, size, original, n, false);
    } else
        absent(r, path);
}

/* Python used POSIX shlex.split(CC) on every platform. Keep quotes, empty arguments,
 * escaped whitespace and the double-quote backslash rules, without invoking a shell. */
static const char **compiler(scenario_run_t *r, size_t *count) {
    const char *cc = getenv("CC");
    if (!cc) {
#ifdef _WIN32
        cc = "gcc";
#else
        cc = "cc";
#endif
    }
    size_t length = strlen(cc);
    const char **args = owned(r, calloc(length + 16, sizeof(*args)));
    char *words = owned(r, malloc(length + 1));
    size_t pos = 0, n = 0;
    while (*cc) {
        while (*cc && strchr(" \t\r\n", *cc))
            cc++;
        if (!*cc)
            break;
        args[n++] = words + pos;
        char quote = 0;
        while (*cc && (quote || !strchr(" \t\r\n", *cc))) {
            char ch = *cc++;
            if (ch == '\'' && quote != '"') {
                if (quote == '\'')
                    quote = 0;
                else
                    quote = '\'';
                continue;
            }
            if (ch == '"' && quote != '\'') {
                if (quote == '"')
                    quote = 0;
                else
                    quote = '"';
                continue;
            }
            if (ch == '\\' && quote != '\'') {
                REQUIRE(*cc);
                if (quote == '"' && *cc != '"' && *cc != '\\')
                    words[pos++] = ch;
                ch = *cc++;
            }
            words[pos++] = ch;
        }
        REQUIRE(!quote);
        words[pos++] = 0;
    }
    REQUIRE(n);
    *count = n;
    return args;
}

static void compile(scenario_run_t *r, const char *source, const char *output, bool shared) {
    size_t n;
    const char **args = compiler(r, &n);
    const char *exe = args[0];
    if (shared) {
        args[n++] = "-shared";
#ifndef _WIN32
        args[n++] = "-fPIC";
#endif
    }
    args[n++] = source;
    args[n++] = "-o";
    args[n++] = output;
#ifndef _WIN32
    if (!shared)
        args[n++] = "-ldl";
#endif
    args[n] = NULL;
    scenario_command_result_t result = language_command(r, exe, args, NULL);
    REQUIRE(result.exit_code == 0);
}

static unsigned char *
extract(scenario_run_t *r, const unsigned char *zip, size_t size, size_t index, size_t *n) {
    char error[256];
    return owned(r,
                 format_zip_extract(zip, size, index, 32 * 1024 * 1024, n, error, sizeof(error)));
}

static void constructor(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *work = scenario_work_directory(scenario_run_context(r));
    const char *source = language_join(r, work, "probe.c"),
               *load_source = language_join(r, work, "load.c"),
               *marker = language_join(r, work, "constructor-ran");
#ifdef _WIN32
    const char *library = language_join(r, work, "probe.dll"),
               *loader = language_join(r, work, "load.exe"), *name = "native/probe.dll";
#else
    const char *library = language_join(r, work, "probe.so"),
               *loader = language_join(r, work, "load"), *name = "native/probe.so";
#endif
    const char probe[] =
        "#include <stdio.h>\n#include <stdlib.h>\n__attribute__((constructor)) static void "
        "probe(void) {\n const char *p = getenv(\"GOAT_VALIDATION_MARKER\");\n if (p) { FILE *f = "
        "fopen(p, \"wb\"); if (f) { fputs(\"loaded\", f); fclose(f); } }\n}\n";
    const char load[] = "#ifdef _WIN32\n#include <windows.h>\nint main(int n, char **a) { return n "
                        "!= 2 || !LoadLibraryA(a[1]); }\n#else\n#include <dlfcn.h>\nint main(int "
                        "n, char **a) { return n != 2 || !dlopen(a[1], RTLD_NOW); }\n#endif\n";
    language_write(r, source, probe, sizeof(probe) - 1);
    language_write(r, load_source, load, sizeof(load) - 1);
    compile(r, source, library, true);
    compile(r, load_source, loader, false);
    const char *const *env = language_environment_override(r, "GOAT_VALIDATION_MARKER", marker);
    const char *args[] = {loader, library, NULL};
    scenario_command_result_t loaded = language_command(r, loader, args, env);
    REQUIRE(loaded.exit_code == 0);
    size_t n;
    const char *mark = language_read(r, marker, &n);
    scenario_require_bytes(r, "constructor positively loaded", "loaded", 6, mark, n, false);
    language_remove(r, marker);
    size_t payload_size;
    const char *payload = language_read(r, library, &payload_size);
    size_t zip_size, json_size, code_size;
    unsigned char *base =
        input(r, fixture(r, c, "integration.module_validation.compiler.valid"), &zip_size);
    unsigned char *raw_json = extract(r, base, zip_size, 0, &json_size),
                  *code = extract(r, base, zip_size, 1, &code_size);
    yyjson_doc *original = parse(r, (const char *)raw_json, json_size);
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(original, NULL);
    REQUIRE(doc);
    scenario_defer(r, free_mut, doc);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc),
                   *native = yyjson_mut_arr_get(yyjson_mut_obj_get(root, "native"), 0);
    char hash[65], error[256];
    format_binary_sha256(payload, payload_size, hash);
    REQUIRE(yyjson_mut_obj_put(native, yyjson_mut_str(doc, "hash"), yyjson_mut_strcpy(doc, hash)));
    REQUIRE(yyjson_mut_obj_put(native, yyjson_mut_str(doc, "library"), yyjson_mut_str(doc, name)));
#ifdef _WIN32
    yyjson_mut_val *target = yyjson_mut_obj_get(native, "target");
    const char windows_json[] = "{\"os\":\"windows\",\"minimumOs\":[10],\"runtime\":{\"family\":"
                                "\"ucrt\",\"minimumVersion\":[0]}}";
    yyjson_doc *windows = parse(r, windows_json, sizeof(windows_json) - 1);
    yyjson_val *key, *value;
    size_t i, len;
    yyjson_obj_foreach(yyjson_doc_get_root(windows), i, len, key, value) {
        REQUIRE(yyjson_mut_obj_put(target,
                                   yyjson_val_mut_copy(doc, key),
                                   yyjson_val_mut_copy(doc, value)));
    }
#endif
    size_t schema_size;
    const char *schema_bytes = language_read(r, c->schema, &schema_size);
    yyjson_mut_doc *schema = format_json_parse(schema_bytes, schema_size, error, sizeof(error));
    REQUIRE(schema);
    scenario_defer(r, free_mut, schema);
    REQUIRE(format_binary_main_hash(root,
                                    yyjson_mut_doc_get_root(schema),
                                    code,
                                    code_size,
                                    hash,
                                    error,
                                    sizeof(error)));
    const char *roles[] = {"compiler", "loader", "inspect", "merge"};
    for (size_t damaged = 0; damaged < 2; damaged++) {
        REQUIRE(yyjson_mut_obj_put(
            root,
            yyjson_mut_str(doc, "mainHash"),
            yyjson_mut_strcpy(
                doc,
                damaged ? "0000000000000000000000000000000000000000000000000000000000000000"
                        : hash)));
        size_t written;
        char *json = owned(r, yyjson_mut_write(doc, 0, &written));
        format_zip_entry_t entries[3];
        format_zip_entry_init(entries, "module.json", json, written, false);
        format_zip_entry_init(entries + 1, "bytecode.bin", code, code_size, false);
        format_zip_entry_init(entries + 2, name, payload, payload_size, false);
        size_t archive_size;
        unsigned char *archive = owned(
            r,
            format_zip_build(entries, 3, NULL, 0, false, &archive_size, error, sizeof(error)));
        language_write(r,
                       language_join(r, work, damaged ? "bad.gbin" : "good.gbin"),
                       archive,
                       archive_size);
        validate(r,
                 c,
                 archive,
                 archive_size,
                 roles[c->mode],
                 "fixture.gbin",
                 NULL,
                 NULL,
                 damaged ? 5 : 0,
                 fixture(r,
                         c,
                         damaged ? "integration.module_validation.hash-main"
                                 : "integration.module_validation.compiler.valid"),
                 name,
                 env);
        absent(r, marker);
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
    int result = 1;
    if (argc == 7 || argc == 8) {
        fs_error_t e = {0};
        char *host = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        char *path = join_fs_path(argv[5], "module-validation/scenarios.json", &e);
        clear_fs_error(&e);
        void *data = NULL;
        size_t size = 0;
        bool ok = host && path && read_fs_regular_file(path, 4 * 1024 * 1024, &data, &size, &e);
        clear_fs_error(&e);
        yyjson_doc *doc = ok ? yyjson_read(data, size, 0) : NULL;
        yyjson_val *all = yyjson_doc_get_root(doc);
        if (yyjson_is_arr(all) && yyjson_arr_size(all)) {
            size_t count = yyjson_arr_size(all) + 9;
            configuration_t *config = CALLOC(count * sizeof(*config));
            scenario_definition_t *defs = CALLOC(count * sizeof(*defs));
            const char *fixed[] = {"api",
                                   "file-default",
                                   "file-exact-limit",
                                   "file-archive-limit",
                                   "file-missing",
                                   "constructor.compiler",
                                   "constructor.loader",
                                   "constructor.inspect",
                                   "constructor.merge"};
            char ids[9][96];
            for (size_t i = 0; i < 9; i++) {
                snprintf(ids[i], sizeof(ids[i]), "integration.module_validation.%s", fixed[i]);
                config[i] = (configuration_t){host,
                                              argv[6],
                                              NULL,
                                              all,
                                              i == 0  ? 0
                                              : i < 5 ? i - 1
                                                      : i - 5};
                defs[i] = (scenario_definition_t){ids[i],
                                                  180000,
                                                  i == 0  ? api
                                                  : i < 5 ? file_case
                                                          : constructor,
                                                  config + i,
                                                  NULL};
            }
            for (size_t i = 9; i < count; i++) {
                yyjson_val *v = yyjson_arr_get(all, i - 9);
                config[i] = (configuration_t){host, argv[6], v, all, 0};
                defs[i] = (scenario_definition_t){yyjson_get_str(yyjson_obj_get(v, "id")),
                                                  60000,
                                                  corpus,
                                                  config + i,
                                                  NULL};
            }
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 8 ? argv[7] : NULL};
            result = scenario_registry_main(defs, count, argc == 8 ? 2 : 1, selection, &options);
            FREE(defs);
            FREE(config);
        } else
            fputs("Cannot read validation corpus\n", stderr);
        yyjson_doc_free(doc);
        FREE(data);
        FREE(host);
        FREE(path);
    } else
        fputs(
            "usage: module_validation_scenarios ID HOST BUILD DATA FIXTURES SCHEMA [--keep-work]\n",
            stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
