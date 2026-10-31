/** @file module_inspect_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Public inspector text/JSON contracts, replaying independent portable inputs (#372).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "test/support/format_binary.h"
#include "test/support/format_json.h"
#include "test/support/format_zip.h"
#include "test/support/inspect_report.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)
typedef yyjson_mut_val inspect_value_t;

typedef struct {
    const void *data;
    size_t size;
} bytes_t;

typedef struct {
    const char *host, *cc, *format;
    inspect_value_t *all, *record;
    bool skipped;
} config_t;

typedef struct {
    scenario_run_t *r;
    config_t *c;
    const char *root, *cwd, *data;
    inspect_value_t *installed;
} suite_t;

typedef struct {
    scenario_command_result_t result;
    const char *out, *err;
    inspect_value_t *json;
} report_t;

static bool free_doc(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool free_plain(void *p) {
    free(p);
    return true;
}

static const char *owned_copy(scenario_run_t *r, const char *p, size_t n) {
    char *s = language_owned(r, ALLOC(n + 1));
    memcpy(s, p, n);
    s[n] = 0;
    return s;
}

static inspect_value_t *get(inspect_value_t *v, const char *k) {
    return yyjson_mut_obj_get(v, k);
}

static const char *str(scenario_run_t *r, inspect_value_t *v) {
    const char *s = yyjson_mut_get_str(v);
    REQUIRE(s && strlen(s) == yyjson_mut_get_len(v));
    return s;
}

static bool eq(inspect_value_t *v, const char *s) {
    return yyjson_mut_is_str(v) && yyjson_mut_get_len(v) == strlen(s)
           && !memcmp(yyjson_mut_get_str(v), s, strlen(s));
}

static bytes_t read_bytes(scenario_run_t *r, const char *path) {
    size_t n;
    const char *data = language_read(r, path, &n);
    return (bytes_t){data, n};
}

static void same(scenario_run_t *r, const char *label, bytes_t a, bytes_t b) {
    scenario_require_bytes(r, label, a.data, a.size, b.data, b.size, false);
}

static bool exists(scenario_run_t *r, const char *path) {
    fs_error_t e = {0};
    fs_info_t info;
    bool ok = inspect_fs_path(path, false, &info, &e), missing = e.status == FS_NOT_FOUND;
    clear_fs_error(&e);
    REQUIRE(ok || missing);
    return ok;
}

static void directory(scenario_run_t *r, const char *path) {
    if (exists(r, path))
        return;
    const char *slash = strrchr(path, '/');
    if (slash) {
        const char *parent = owned_copy(r, path, (size_t)(slash - path));
        if (*parent)
            directory(r, parent);
    }
    language_directory(r, path);
}

static void put(scenario_run_t *r, const char *path, bytes_t data) {
    const char *slash = strrchr(path, '/');
    if (slash)
        directory(r, owned_copy(r, path, (size_t)(slash - path)));
    language_write(r, path, data.data, data.size);
}

static inspect_value_t *parse(scenario_run_t *r, bytes_t input) {
    char error[256];
    yyjson_mut_doc *doc = inspect_report_parse(input.data, input.size, error, sizeof(error));
    scenario_require(r, doc != NULL, error);
    scenario_defer(r, free_doc, doc);
    return yyjson_mut_doc_get_root(doc);
}

static char *normalize(scenario_run_t *r, const void *data, size_t size) {
    string_value_t decoded = decode_utf8_n(data, size);
    bool valid = decoded.data != NULL;
    FREE_STRING(decoded);
    REQUIRE(valid);
    char *out = language_owned(r, ALLOC(size + 1));
    size_t n = 0;
    for (size_t i = 0; i < size; i++) {
        if (((const char *)data)[i] == '\r' && i + 1 < size && ((const char *)data)[i + 1] == '\n')
            continue;
        out[n++] = ((const char *)data)[i];
    }
    out[n] = 0;
    REQUIRE(strlen(out) == n);
    return out;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static const char *expand(suite_t *s, inspect_value_t *input) {
    scenario_run_t *r = s->r;
    if (yyjson_mut_is_obj(input)) {
        const char *hex = str(r, get(input, "hex"));
        size_t n = strlen(hex);
        REQUIRE(n % 2 == 0);
        char *bytes = language_owned(r, ALLOC(n / 2 + 1));
        for (size_t i = 0; i < n; i += 2) {
            int a = hex_digit(hex[i]), b = hex_digit(hex[i + 1]);
            REQUIRE(a >= 0 && b >= 0);
            bytes[i / 2] = (char)(a * 16 + b);
            REQUIRE(bytes[i / 2] != 0);
        }
        bytes[n / 2] = 0;
        return bytes;
    }
    const char *text = str(r, input), *token = "@ROOT@";
    size_t n = strlen(text), root_size = strlen(s->root), matches = 0;
    for (const char *p = text; (p = strstr(p, token)); p += 6)
        matches++;
    char *out = language_owned(r, ALLOC(n + matches * root_size + 1));
    char *dest = out;
    const char *p = text, *q;
    while ((q = strstr(p, token))) {
        size_t prefix = (size_t)(q - p);
        memcpy(dest, p, prefix);
        dest += prefix;
        memcpy(dest, s->root, root_size);
        dest += root_size;
        p = q + 6;
    }
    strcpy(dest, p);
    return out;
}

static bytes_t blob(suite_t *s, inspect_value_t *hash_value) {
    scenario_run_t *r = s->r;
    const char *hash = str(r, hash_value);
    REQUIRE(strlen(hash) == 64);
    for (size_t i = 0; i < 64; i++)
        REQUIRE(hex_digit(hash[i]) >= 0);
    bytes_t b = read_bytes(r, language_join(r, language_join(r, s->data, "blobs"), hash));
    char actual[65];
    format_binary_sha256(b.data, b.size, actual);
    REQUIRE(!strcmp(actual, hash));
    return b;
}

static void materialize(suite_t *s, inspect_value_t *record) {
    scenario_run_t *r = s->r;
    inspect_value_t *files = get(record, "files");
    REQUIRE(yyjson_mut_is_obj(files));
    size_t i, n;
    inspect_value_t *key, *value;
    if (s->installed) {
        yyjson_mut_obj_foreach(s->installed, i, n, key, value) {
            const char *name = str(r, key);
            if (!get(files, name)) {
                const char *path = language_join(r, s->root, name);
                if (exists(r, path))
                    language_remove(r, path);
            }
        }
    }
    yyjson_mut_obj_foreach(files, i, n, key, value) {
        const char *name = str(r, key);
        REQUIRE(*name && *name != '/' && !strstr(name, ".."));
        put(r, language_join(r, s->root, name), blob(s, value));
    }
    s->installed = files;
}

static const char *const *environment(suite_t *s, inspect_value_t *overrides) {
    scenario_run_t *r = s->r;
    const char *const *base = language_environment_override(r, "GOAT_MODULE_PATH", "");
    size_t count = 0;
    while (base[count])
        count++;
    size_t extra = yyjson_mut_obj_size(overrides);
    const char **env = language_owned(r, CALLOC((count + extra + 1) * sizeof(*env)));
    size_t used = 0;
    for (size_t i = 0; i < count; i++) {
        const char *equals = strchr(base[i], '=');
        REQUIRE(equals);
        const char *name = owned_copy(r, base[i], (size_t)(equals - base[i]));
        if (!get(overrides, name))
            env[used++] = base[i];
    }
    size_t i, n;
    inspect_value_t *key, *value;
    yyjson_mut_obj_foreach(overrides, i, n, key, value) {
        const char *name = str(r, key), *text = expand(s, value);
        size_t a = strlen(name), b = strlen(text);
        char *pair = language_owned(r, ALLOC(a + b + 2));
        memcpy(pair, name, a);
        pair[a] = '=';
        memcpy(pair + a + 1, text, b + 1);
        env[used++] = pair;
    }
    return env;
}

static bool has_code(inspect_value_t *report, const char *code, const char *severity) {
    size_t i, n;
    inspect_value_t *d;
    yyjson_mut_arr_foreach(get(report, "diagnostics"),
                           i,
                           n,
                           d) if (eq(get(d, "code"), code)
                                  && (!severity || eq(get(d, "severity"), severity))) return true;
    return false;
}

static size_t code_count(inspect_value_t *report, const char *code, const char *severity) {
    size_t count = 0, i, n;
    inspect_value_t *d;
    yyjson_mut_arr_foreach(get(report, "diagnostics"),
                           i,
                           n,
                           d) if ((!code || eq(get(d, "code"), code))
                                  && (!severity || eq(get(d, "severity"), severity))) count++;
    return count;
}

static inspect_value_t *root_module(suite_t *s, inspect_value_t *report) {
    scenario_run_t *r = s->r;
    const char *main = language_join(r, s->root, "module.gbin");
    size_t i, n;
    inspect_value_t *m;
    yyjson_mut_arr_foreach(get(report, "modules"), i, n, m) if (eq(get(m, "archive"),
                                                                   main)) return m;
    REQUIRE(false);
    return NULL;
}

static int base64_digit(char c) {
    const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const char *p = strchr(alphabet, c);
    return p && c ? (int)(p - alphabet) : -1;
}

static void canonical_base64(scenario_run_t *r, const char *text) {
    size_t n = strlen(text);
    REQUIRE(n % 4 == 0);
    for (size_t i = 0; i < n; i += 4) {
        int a = base64_digit(text[i]), b = base64_digit(text[i + 1]), c = base64_digit(text[i + 2]),
            d = base64_digit(text[i + 3]);
        REQUIRE(a >= 0 && b >= 0);
        if (text[i + 2] == '=')
            REQUIRE(i + 4 == n && text[i + 3] == '=' && (b & 15) == 0);
        else if (text[i + 3] == '=')
            REQUIRE(i + 4 == n && c >= 0 && (c & 3) == 0);
        else
            REQUIRE(c >= 0 && d >= 0);
    }
}

static void common_json(suite_t *s, inspect_value_t *report, int status) {
    scenario_run_t *r = s->r;
    inspect_value_t *schema =
        parse(r, read_bytes(r, language_join(r, s->c->format, "inspect-report.schema.json")));
    char error[512];
    scenario_require(
        r,
        inspect_report_schema(report, schema, schema, s->c->format, error, sizeof(error)),
        error);
    REQUIRE(yyjson_mut_is_uint(get(report, "reportVersion"))
            && yyjson_mut_get_uint(get(report, "reportVersion")) == 1);
    REQUIRE(yyjson_mut_get_sint(get(report, "exitCode")) == status
            && yyjson_mut_get_bool(get(report, "success")) == (status == 0));
    if (status)
        REQUIRE(code_count(report, NULL, "error") > 0);
    size_t i, n;
    inspect_value_t *d;
    yyjson_mut_arr_foreach(get(report, "diagnostics"), i, n, d) {
        inspect_value_t *entry = get(d, "entryBase64");
        if (entry)
            canonical_base64(r, str(r, entry));
    }
    inspect_value_t *m;
    yyjson_mut_arr_foreach(get(report, "modules"), i, n, m) {
        inspect_value_t *metadata = get(m, "metadata"), *exports = get(m, "exports"),
                        *native = get(m, "native");
        if (yyjson_mut_is_null(metadata)) {
            REQUIRE(yyjson_mut_is_null(exports) && yyjson_mut_is_null(native));
            continue;
        }
        inspect_value_t *external = get(metadata, "externals");
        REQUIRE(yyjson_mut_arr_size(exports) == yyjson_mut_obj_size(external));
        size_t j, k;
        inspect_value_t *e;
        const char *previous = NULL;
        yyjson_mut_arr_foreach(exports, j, k, e) {
            const char *name = str(r, get(e, "name"));
            if (previous)
                REQUIRE(strcmp(previous, name) < 0);
            previous = name;
            inspect_value_t *source = get(external, name);
            REQUIRE(source && format_json_equal(get(e, "mutable"), get(source, "mutable")));
            REQUIRE((get(e, "type") != NULL) == (get(source, "type") != NULL));
            if (get(source, "type"))
                REQUIRE(format_json_equal(get(e, "type"), get(source, "type")));
        }
        yyjson_mut_arr_foreach(native, j, k, e) {
            inspect_value_t *compat = get(e, "compatibility"), *loading = get(e, "loading");
            REQUIRE(yyjson_mut_obj_size(compat) == 1 && get(compat, "state"));
            if (eq(get(loading, "state"), "not_checked"))
                REQUIRE(yyjson_mut_obj_size(loading) == 2 && get(loading, "state")
                        && get(loading, "reason"));
            else
                REQUIRE(yyjson_mut_obj_size(loading) == 3 && get(loading, "state")
                        && get(loading, "probe") && get(loading, "cleanup"));
        }
    }
}

static bytes_t serialize(scenario_run_t *r, inspect_value_t *value) {
    size_t n;
    char *data = format_json_canonical(value, &n);
    REQUIRE(data);
    scenario_defer(r, free_plain, data);
    return (bytes_t){data, n};
}

static void constructor(suite_t *s, inspect_value_t *record) {
    scenario_run_t *r = s->r;
#ifdef _WIN32
    const char *filename = "constructor.dll", *entry = "native/constructor.dll";
#else
    const char *filename = "constructor.so", *entry = "native/constructor.so";
#endif
    const char *source = language_join(r, s->root, "constructor.c"),
               *library = language_join(r, s->root, filename);
    const char *arguments[] = {s->c->cc,
#ifndef _WIN32
                               "-fPIC",
#endif
                               "-shared",
                               source,
                               "-o",
                               library,
                               NULL};
    child_options_t options = {.executable = s->c->cc,
                               .arguments = arguments,
                               .working_directory = s->root,
                               .empty_stdin = true};
    scenario_command(r, &options, 0);
    bytes_t payload = read_bytes(r, library), code = blob(s, get(record, "constructor_bytecode"));
    inspect_value_t *metadata = parse(r, blob(s, get(record, "constructor_metadata"))),
                    *native = yyjson_mut_arr_get(get(metadata, "native"), 0),
                    *target = get(native, "target");
    REQUIRE(native && target);
    REQUIRE(yyjson_mut_set_str(get(native, "library"), entry));
    char *hash = language_owned(r, ALLOC(65));
    format_binary_sha256(payload.data, payload.size, hash);
    if (!yyjson_mut_get_bool(get(record, "constructor_bad_hash")))
        REQUIRE(yyjson_mut_set_str(get(native, "hash"), hash));
#ifdef _WIN32
    REQUIRE(yyjson_mut_set_str(get(target, "os"), "windows"));
    inspect_value_t *runtime = get(target, "runtime");
    REQUIRE(yyjson_mut_set_str(get(runtime, "family"), "msvcrt"));
    yyjson_mut_doc *factory = yyjson_mut_doc_new(NULL);
    REQUIRE(factory);
    scenario_defer(r, free_doc, factory);
    inspect_value_t *minimum = yyjson_mut_arr(factory);
    REQUIRE(minimum && yyjson_mut_arr_add_uint(factory, minimum, 0));
    REQUIRE(yyjson_mut_obj_put(runtime, yyjson_mut_str(factory, "minimumVersion"), minimum));
#endif
    REQUIRE(yyjson_mut_get_uint(yyjson_mut_arr_get(get(target, "minimumOs"), 0)) == 999);
    inspect_value_t *args = get(record, "args");
    const char *profile = NULL;
    for (size_t i = 0; i + 1 < yyjson_mut_arr_size(args); i++)
        if (eq(yyjson_mut_arr_get(args, i), "--target"))
            profile = expand(s, yyjson_mut_arr_get(args, i + 1));
    REQUIRE(profile);
    put(r, profile, serialize(r, target));
    bytes_t json = serialize(r, metadata);
    format_zip_entry_t files[3];
    format_zip_entry_init(files, "module.json", json.data, json.size, false);
    format_zip_entry_init(files + 1, "bytecode.bin", code.data, code.size, false);
    format_zip_entry_init(files + 2, entry, payload.data, payload.size, false);
    size_t size;
    char error[256];
    void *zip = format_zip_build(files, 3, NULL, 0, false, &size, error, sizeof(error));
    REQUIRE(zip);
    scenario_defer(r, free_plain, zip);
    put(r, language_join(r, s->root, "module.gbin"), (bytes_t){zip, size});
    REQUIRE(!exists(r, language_join(r, s->root, "constructor-was-called")));
}

static report_t run_record(suite_t *s, inspect_value_t *record) {
    scenario_run_t *r = s->r;
    materialize(s, record);
    if (yyjson_mut_get_bool(get(record, "constructor")))
        constructor(s, record);
    inspect_value_t *values = get(record, "args");
    size_t count = yyjson_mut_arr_size(values);
    const char **args = language_owned(r, CALLOC((count + 2) * sizeof(*args)));
    args[0] = s->c->host;
    for (size_t i = 0; i < count; i++)
        args[i + 1] = expand(s, yyjson_mut_arr_get(values, i));
    const char *main = language_join(r, s->root, "module.gbin");
    bool has_main = exists(r, main);
    bytes_t before = has_main ? read_bytes(r, main) : (bytes_t){0};
    child_options_t options = {.executable = s->c->host,
                               .arguments = args,
                               .working_directory = s->cwd,
                               .environment = environment(s, get(record, "env")),
                               .empty_stdin = true};
    scenario_command_result_t result = scenario_command_capture(r, &options);
    int status = (int)yyjson_mut_get_sint(get(record, "status"));
    REQUIRE(result.exit_code == status);
    if (has_main)
        same(r, "inspection never modifies archive", before, read_bytes(r, main));
    const char *out = normalize(r, result.out, result.out_size),
               *err = normalize(r, result.err, result.err_size);
    report_t report = {result, out, err, NULL};
    if (eq(get(record, "kind"), "json")) {
        REQUIRE(result.err_size == 0);
        report.json = parse(r, (bytes_t){result.out, result.out_size});
        common_json(s, report.json, status);
    } else {
        if (status)
            REQUIRE(result.out_size || result.err_size);
        REQUIRE(!strchr(out, 27) && !strchr(err, 27));
        size_t total = strlen(out) + strlen(err);
        char *combined = language_owned(r, ALLOC(total + 1));
        strcpy(combined, out);
        strcat(combined, err);
        size_t i, n;
        inspect_value_t *v;
        yyjson_mut_arr_foreach(get(record, "contains"), i, n, v)
            REQUIRE(strstr(combined, expand(s, v)));
        yyjson_mut_arr_foreach(get(record, "absent"), i, n, v)
            REQUIRE(!strstr(combined, expand(s, v)));
    }
    if (yyjson_mut_get_bool(get(record, "constructor")))
        REQUIRE(!exists(r, language_join(r, s->root, "constructor-was-called")));
    return report;
}

static void state_text(scenario_run_t *r, const char *text, const char *label, const char *state) {
    size_t n = strlen(text);
    char *lower = language_owned(r, ALLOC(n + 1));
    for (size_t i = 0; i < n; i++)
        lower[i] = (char)tolower((unsigned char)text[i]);
    lower[n] = 0;
    const char *p = strstr(lower, label);
    REQUIRE(p);
    p += strlen(label);
    while (isspace((unsigned char)*p))
        p++;
    REQUIRE(!strncmp(p, state, strlen(state)));
}

static size_t module_mentions(scenario_run_t *r, const char *text, const char *name) {
    size_t count = 0;
    const char *p = text;
    while ((p = strstr(p, "Module:"))) {
        p += 7;
        while (isspace((unsigned char)*p))
            p++;
        if (*p == '"' && !strncmp(p + 1, name, strlen(name)) && p[1 + strlen(name)] == '"')
            count++;
    }
    (void)r;
    return count;
}

static void text_checks(suite_t *s, inspect_value_t *record, report_t report) {
    scenario_run_t *r = s->r;
    unsigned line = (unsigned)yyjson_mut_get_uint(get(record, "source_line"));
    const char *out = report.out;
    if (line == 127 || line == 128) {
        state_text(r, out, "validation:", "passed");
        state_text(r, out, "loading:", "not checked");
    }
    if (line == 168 || line == 177) {
        const char *a = strstr(out, "a-known"), *b = strstr(out, "control"),
                   *c = strstr(out, "z-unknown");
        REQUIRE(a && b && c && a < b && b < c);
        REQUIRE(strstr(out, "\"a-known\" mutable=false type=\"integer\"")
                && strstr(out, "\"z-unknown\" mutable=true type=unknown"));
        REQUIRE(!strstr(out, "control\n\x1b") && (strstr(out, "\\n") || strstr(out, "\\u000a")));
    }
    if (line == 185) {
        REQUIRE(!strchr(out, 127) && !strstr(out, "\xc2\x9b"));
        REQUIRE(strstr(out, "\\u007f") && strstr(out, "\\u009b"));
        const char *raw[] = {"\xe2\x80\xa8",
                             "\xe2\x80\xa9",
                             "\xe2\x80\xae",
                             "\xe2\x81\xa6",
                             "\xe2\x81\xa9"},
                   *escaped[] = {"\\u2028", "\\u2029", "\\u202e", "\\u2066", "\\u2069"};
        for (size_t i = 0; i < 5; i++)
            REQUIRE(!strstr(out, raw[i]) && strstr(out, escaped[i]));
    }
    if (line == 214 || line == 218)
        REQUIRE(!strstr(out, "Main hash:"));
    if (line == 218)
        REQUIRE(!strstr(out, "Module:"));
    if (line == 314 || line == 318)
        REQUIRE(module_mentions(r, out, "shared") == 1);
    if (line == 340)
        REQUIRE(module_mentions(r, out, "shared") == 2);
}

static bytes_t zip_member(scenario_run_t *r, bytes_t archive, const char *name) {
    format_zip_info_t info;
    char error[256];
    REQUIRE(format_zip_inspect(archive.data, archive.size, SIZE_MAX, &info, error, sizeof(error)));
    size_t count = info.count;
    for (size_t i = 0; i < count; i++) {
        REQUIRE(format_zip_inspect(archive.data, archive.size, i, &info, error, sizeof(error)));
        if (info.name_size == strlen(name) && !memcmp(info.name, name, info.name_size)) {
            size_t n;
            void *data = format_zip_extract(archive.data,
                                            archive.size,
                                            i,
                                            16 * 1024 * 1024,
                                            &n,
                                            error,
                                            sizeof(error));
            REQUIRE(data);
            scenario_defer(r, free_plain, data);
            return (bytes_t){data, n};
        }
    }
    REQUIRE(false);
    return (bytes_t){0};
}

static inspect_value_t *input_metadata(suite_t *s) {
    scenario_run_t *r = s->r;
    return parse(
        r,
        zip_member(r, read_bytes(r, language_join(r, s->root, "module.gbin")), "module.json"));
}

static void require_error_only(scenario_run_t *r, inspect_value_t *report, const char *code) {
    REQUIRE(has_code(report, code, "error") && code_count(report, NULL, "error") == 1);
}

static bool no_metadata(inspect_value_t *report) {
    size_t i, n;
    inspect_value_t *m;
    yyjson_mut_arr_foreach(get(report, "modules"),
                           i,
                           n,
                           m) if (!yyjson_mut_is_null(get(m, "metadata"))) return false;
    return true;
}

static inspect_value_t *native_named(scenario_run_t *r, inspect_value_t *array, const char *name) {
    size_t i, n;
    inspect_value_t *v;
    yyjson_mut_arr_foreach(array, i, n, v) if (eq(get(v, "library"), name)) return v;
    REQUIRE(false);
    return NULL;
}

static void json_checks(suite_t *s, inspect_value_t *record, report_t report) {
    scenario_run_t *r = s->r;
    inspect_value_t *json = report.json, *modules = get(json, "modules");
    unsigned line = (unsigned)yyjson_mut_get_uint(get(record, "source_line"));
    if (line == 409 || line == 410) {
        inspect_value_t *target = get(json, "target"), *m = root_module(s, json),
                        *metadata = get(m, "metadata");
        REQUIRE(eq(get(target, "source"), "current")
                && yyjson_mut_is_bool(get(target, "littleEndian")));
        REQUIRE(!get(metadata, "reportVersion"));
        REQUIRE(format_json_equal(get(metadata, "mainHash"), get(input_metadata(s), "mainHash")));
        REQUIRE(eq(get(m, "validation"), "passed"));
        REQUIRE(yyjson_mut_arr_size(get(m, "exports")) == 0
                && yyjson_mut_arr_size(get(m, "native")) == 0);
        REQUIRE(has_code(json, "NO_NATIVE_VARIANT", "warning"));
    }
    if (line == 413)
        REQUIRE(eq(get(get(root_module(s, json), "metadata"), "version"), "2026-special"));
    if (line == 429) {
        inspect_value_t *m = yyjson_mut_arr_get(modules, 0),
                        *baseline = yyjson_mut_arr_get(s->c->all, 0);
        bytes_t original = blob(s, get(get(baseline, "files"), "module.gbin"));
        inspect_value_t *metadata = parse(r, zip_member(r, original, "module.json"));
        REQUIRE(format_json_equal(get(get(m, "metadata"), "mainHash"), get(metadata, "mainHash")));
    }
    if (line == 433 || line == 436) {
        REQUIRE(has_code(json, "ARCHIVE_IO", NULL) && !yyjson_mut_arr_size(modules));
    }
    if (line == 441 || line == 444) {
        REQUIRE(has_code(json, "INVALID_ARGUMENT", NULL) && !yyjson_mut_arr_size(modules));
    }
    if (line == 448) {
        require_error_only(r, json, "INVALID_ARGUMENT");
        REQUIRE(yyjson_mut_is_null(get(json, "input")) && !yyjson_mut_arr_size(modules));
    }
    if (line == 452) {
        require_error_only(r, json, "ARCHIVE_IO");
        REQUIRE(!yyjson_mut_arr_size(modules));
    }
    if (line == 456) {
        inspect_value_t *information = get(json, "information");
        const char *flag = str(r, yyjson_mut_arr_get(get(record, "args"), 1));
        REQUIRE(eq(get(information, "kind"), flag + 2));
        REQUIRE(*str(r, get(information, "text")) && !yyjson_mut_arr_size(modules));
    }
    if (line == 459)
        REQUIRE(has_code(json, "TARGET_IO", NULL));
    if (line == 463)
        REQUIRE(has_code(json, "TARGET_INVALID", NULL) && !yyjson_mut_arr_size(modules));
    if (line == 476 || line == 487) {
        inspect_value_t *retained = get(root_module(s, json), "metadata"),
                        *input = input_metadata(s);
        const char *fields[] = {"description", "license", "homepage"};
        for (size_t i = 0; i < 3; i++)
            REQUIRE(format_json_equal(get(retained, fields[i]), get(input, fields[i])));
        inspect_value_t *authors = get(retained, "authors");
        REQUIRE(yyjson_mut_arr_size(authors) == 2);
        inspect_value_t *first = yyjson_mut_arr_get(authors, 0),
                        *second = yyjson_mut_arr_get(authors, 1);
        REQUIRE(yyjson_mut_obj_size(first) == 1 && eq(get(first, "name"), "Имя 😀"));
        REQUIRE(yyjson_mut_obj_size(second) == 2 && eq(get(second, "name"), "Second")
                && eq(get(second, "email"), "x@y.test"));
        inspect_value_t *producers = get(retained, "producers"),
                        *expected = get(input, "producers");
        REQUIRE(yyjson_mut_arr_size(producers) == yyjson_mut_arr_size(expected));
        size_t i, n;
        inspect_value_t *item;
        yyjson_mut_arr_foreach(expected, i, n, item) {
            size_t matches = 0, j, k;
            inspect_value_t *actual;
            yyjson_mut_arr_foreach(producers, j, k, actual) matches +=
                format_json_equal(item, actual);
            REQUIRE(matches == 1);
        }
        REQUIRE(!strchr(report.out, 27));
    }
    if (line == 491) {
        inspect_value_t *input = input_metadata(s),
                        *retained = get(root_module(s, json), "metadata");
        const char *fields[] = {"description", "license", "homepage", "authors"};
        size_t checked = 0;
        for (size_t i = 0; i < 4; i++)
            if (get(input, fields[i])) {
                REQUIRE(!get(retained, fields[i]));
                checked++;
            }
        REQUIRE(checked == 1);
    }
    if (line == 496) {
        inspect_value_t *v =
            get(get(get(get(root_module(s, json), "metadata"), "externals"), "exact"), "value");
        REQUIRE(yyjson_mut_is_uint(v) && yyjson_mut_get_uint(v) == UINT64_C(9007199254740993));
    }
    if (line == 500) {
        inspect_value_t *m = get(root_module(s, json), "metadata");
        REQUIRE(eq(get(m, "description"), " ")
                && eq(get(yyjson_mut_arr_get(get(m, "authors"), 0), "email"), " "));
    }
    if (line == 506)
        REQUIRE(yyjson_mut_is_null(get(root_module(s, json), "metadata")));
    if (line == 512 || line == 516 || line == 517 || line == 518) {
        inspect_value_t *m = root_module(s, json);
        REQUIRE(has_code(json, "MAIN_HASH_MISMATCH", NULL));
        REQUIRE(eq(get(get(m, "metadata"), "name"), "retained-json")
                && eq(get(m, "validation"), "failed"));
    }
    if (line == 522) {
        bool name = false, version = false;
        size_t i, n;
        inspect_value_t *d;
        yyjson_mut_arr_foreach(get(json, "diagnostics"), i, n, d) {
            name |= eq(get(d, "field"), "/name");
            version |= eq(get(d, "field"), "/version");
        }
        REQUIRE(name && version && code_count(json, NULL, "error") >= 2);
    }
    if (line == 527) {
        REQUIRE(has_code(json, "JSON_INVALID", NULL)
                && !has_code(json, "MAIN_HASH_MISMATCH", NULL));
        REQUIRE(yyjson_mut_is_null(get(root_module(s, json), "metadata")));
        size_t count = 0, i, n;
        inspect_value_t *d;
        yyjson_mut_arr_foreach(get(json, "diagnostics"), i, n, d) if (eq(get(d, "severity"),
                                                                         "skipped")) {
            count++;
            REQUIRE(get(d, "prerequisite"));
        }
        REQUIRE(count > 0);
    }
    if (line == 535) {
        REQUIRE(has_code(json, "ZIP_INVALID", NULL) || has_code(json, "CONTAINER_INVALID", NULL));
        REQUIRE(no_metadata(json) && !has_code(json, "MAIN_HASH_MISMATCH", NULL));
    }
    if (line == 544) {
        inspect_value_t *target = get(json, "target"),
                        *profile =
                            parse(r, read_bytes(r, language_join(r, s->root, "json-target.json")));
        REQUIRE(eq(get(target, "source"), "explicit"));
        const char *fields[] = {"os", "architecture", "bits"};
        for (size_t i = 0; i < 3; i++)
            REQUIRE(format_json_equal(get(target, fields[i]), get(profile, fields[i])));
        inspect_value_t *m = root_module(s, json), *native = get(m, "native");
        REQUIRE(eq(get(get(native_named(r, native, "native/test.so"), "compatibility"), "state"),
                   "compatible"));
        REQUIRE(eq(get(get(native_named(r, native, "native/other.so"), "compatibility"), "state"),
                   "incompatible"));
        inspect_value_t *metadata_native = get(get(m, "metadata"), "native");
        REQUIRE(yyjson_mut_arr_size(metadata_native) == 2);
        REQUIRE(eq(get(get(yyjson_mut_arr_get(metadata_native, 0), "cCompiler"), "version"),
                   "17.4 fixture"));
    }
    if (line == 557) {
        size_t i, n;
        inspect_value_t *v;
        yyjson_mut_arr_foreach(get(get(root_module(s, json), "metadata"), "native"), i, n, v)
            REQUIRE(!get(v, "cCompiler"));
    }
    if (line == 560) {
        REQUIRE(has_code(json, "NATIVE_HASH_MISMATCH", NULL));
        size_t i, n;
        inspect_value_t *v;
        yyjson_mut_arr_foreach(get(root_module(s, json), "native"), i, n, v)
            REQUIRE(eq(get(get(v, "compatibility"), "state"), "not_checked"));
    }
    if (line == 585 || line == 590) {
        size_t shared = 0, i, n;
        inspect_value_t *m;
        const char *previous = NULL;
        yyjson_mut_arr_foreach(modules, i, n, m) {
            const char *name = str(r, get(get(m, "metadata"), "name"));
            if (previous)
                REQUIRE(strcmp(previous, name) <= 0);
            previous = name;
            shared += !strcmp(name, "shared");
        }
        REQUIRE(shared == 1);
    }
    if (line == 592 || line == 596) {
        REQUIRE(has_code(json, "DEPENDENCY_NOT_FOUND", NULL));
        inspect_value_t *paths[2] = {NULL, NULL};
        size_t count = 0, i, n;
        inspect_value_t *d;
        yyjson_mut_arr_foreach(get(json, "diagnostics"), i, n, d) if (eq(get(d, "code"),
                                                                         "DEPENDENCY_NOT_FOUND")) {
            REQUIRE(count < 2);
            paths[count++] = get(d, "path");
        }
        REQUIRE(count == 2 && paths[0] && paths[1] && !format_json_equal(paths[0], paths[1]));
    }
    if (line == 600)
        REQUIRE(has_code(json, "DEPENDENCY_HASH_MISMATCH", NULL));
    if (line == 603)
        REQUIRE(has_code(json, "DEPENDENCY_CYCLE", NULL));
    if (line == 609)
        REQUIRE(has_code(json, "REFERENCE_INVALID", NULL));
    if (line == 622) {
        size_t count = 0, i, n;
        inspect_value_t *d;
        yyjson_mut_arr_foreach(get(json, "diagnostics"), i, n, d) if (eq(get(d, "code"),
                                                                         "REFERENCE_SKIPPED")) {
            count++;
            REQUIRE(eq(get(d, "severity"), "skipped") && eq(get(d, "check"), "references")
                    && !get(d, "prerequisite"));
        }
        REQUIRE(count && !has_code(json, "REFERENCE_INVALID", NULL));
        unsigned ordinal = (unsigned)yyjson_mut_get_uint(get(record, "ordinal"));
        REQUIRE(has_code(json,
                         ordinal == 132 ? "DEPENDENCY_NOT_FOUND" : "DEPENDENCY_HASH_MISMATCH",
                         NULL));
    }
}

static unsigned previous_case(unsigned ordinal) {
    switch (ordinal) {
        case 2:
            return 1;
        case 33:
            return 32;
        case 38:
            return 37;
        case 62:
            return 61;
        case 72:
            return 71;
        case 79:
            return 78;
        case 98:
            return 97;
        case 109:
            return 108;
        case 111:
            return 110;
        case 126:
            return 125;
        case 128:
            return 127;
        default:
            return 0;
    }
}

static void compare_codes(scenario_run_t *r, inspect_value_t *a, inspect_value_t *b) {
    inspect_value_t *x = get(a, "diagnostics"), *y = get(b, "diagnostics");
    REQUIRE(yyjson_mut_arr_size(x) == yyjson_mut_arr_size(y));
    for (size_t i = 0; i < yyjson_mut_arr_size(x); i++)
        REQUIRE(format_json_equal(get(yyjson_mut_arr_get(x, i), "code"),
                                  get(yyjson_mut_arr_get(y, i), "code")));
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
#ifdef _WIN32
    if (yyjson_mut_get_bool(get(c->record, "posix_only"))) {
        c->skipped = true;
        puts("SKIP: Windows UTF-16 process boundary cannot supply invalid UTF-8 argv/environment "
             "bytes");
        return;
    }
#endif
    const char *root = scenario_work_directory(scenario_run_context(r));
    suite_t s = {
        r,
        c,
        root,
        language_join(r, root, "unrelated"),
        language_join(r, scenario_data_directory(scenario_run_context(r)), "module-inspect"),
        NULL};
    directory(r, s.cwd);
    unsigned ordinal = (unsigned)yyjson_mut_get_uint(get(c->record, "ordinal")),
             previous = previous_case(ordinal);
    report_t before = {0};
    if (previous)
        before = run_record(&s, yyjson_mut_arr_get(c->all, previous - 1));
    report_t report = run_record(&s, c->record);
    if (report.json)
        json_checks(&s, c->record, report);
    else
        text_checks(&s, c->record, report);
    if (previous) {
        if (ordinal == 111)
            compare_codes(r, before.json, report.json);
        else if (ordinal == 79)
            REQUIRE(format_json_equal(before.json, report.json));
        else {
            if (report.json)
                same(r,
                     "deterministic raw JSON stdout",
                     (bytes_t){before.result.out, before.result.out_size},
                     (bytes_t){report.result.out, report.result.out_size});
            else
                same(r,
                     "deterministic stdout",
                     (bytes_t){before.out, strlen(before.out)},
                     (bytes_t){report.out, strlen(report.out)});
            if (ordinal == 2 || ordinal == 38)
                same(r,
                     "deterministic stderr",
                     (bytes_t){before.err, strlen(before.err)},
                     (bytes_t){report.err, strlen(report.err)});
        }
    }
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t argument_error = {0};
    if (!fs_arguments(&argc, &argv, &argument_error)) {
        clear_fs_error(&argument_error);
        return 1;
    }
#endif
    int result = 1;
    if (argc == 7 || (argc == 8 && !strcmp(argv[7], "--keep-work"))) {
        fs_error_t error = {0};
        char *host = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        char *cc;
        if (strchr(argv[3], '/') || strchr(argv[3], '\\')) {
            cc = absolute_fs_path(argv[3], &error);
            clear_fs_error(&error);
        } else {
            cc = ALLOC(strlen(argv[3]) + 1);
            strcpy(cc, argv[3]);
        }
        char *directory_name = join_fs_path(argv[5], "module-inspect/cases.json", &error);
        clear_fs_error(&error);
        void *data = NULL;
        size_t size = 0;
        bool ok = directory_name
                  && read_fs_regular_file(directory_name, 16 * 1024 * 1024, &data, &size, &error);
        clear_fs_error(&error);
        char diagnostic[256];
        yyjson_mut_doc *doc =
            ok ? inspect_report_parse(data, size, diagnostic, sizeof(diagnostic)) : NULL;
        inspect_value_t *all = yyjson_mut_doc_get_root(doc);
        size_t count = yyjson_mut_arr_size(all);
        if (host && cc && count == 133) {
            config_t *configs = CALLOC(count * sizeof(*configs));
            scenario_definition_t *definitions = CALLOC(count * sizeof(*definitions));
            char(*ids)[96] = CALLOC(count * sizeof(*ids));
            for (size_t i = 0; i < count; i++) {
                inspect_value_t *record = yyjson_mut_arr_get(all, i);
                const char *id = yyjson_mut_get_str(get(record, "id"));
                if (!id)
                    break;
                snprintf(ids[i], sizeof(ids[i]), "integration.module_inspect.%s", id);
                configs[i] = (config_t){host, cc, argv[6], all, record, false};
                definitions[i] = (scenario_definition_t){
                    ids[i],
                    yyjson_mut_get_bool(get(record, "constructor")) ? 90000 : 60000,
                    body,
                    configs + i,
                    NULL};
            }
            scenario_options_t options = {.build_directory = argv[4], .data_directory = argv[5]};
            const char *selection[] = {argv[1], argc == 8 ? "--keep-work" : NULL};
            result =
                scenario_registry_main(definitions, count, argc == 8 ? 2 : 1, selection, &options);
            if (!result)
                for (size_t i = 0; i < count; i++)
                    if (configs[i].skipped)
                        result = 77;
            FREE(ids);
            FREE(definitions);
            FREE(configs);
        } else
            fputs("Cannot load inspector corpus or executable paths\n", stderr);
        yyjson_mut_doc_free(doc);
        FREE(data);
        FREE(directory_name);
        FREE(cc);
        FREE(host);
    } else
        fputs(
            "usage: module_inspect_scenarios ID INSPECT CC BUILD DATA FORMAT_ROOT [--keep-work]\n",
            stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
