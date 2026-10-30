/** @file module_source_diagnostics_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Immutable compiled source mappings and diagnostics (#363).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/module_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(x) scenario_require(r, (x), #x)

typedef struct {
    const char *host;
    yyjson_val *fixture;
} configuration_t;

typedef struct {
    yyjson_doc *metadata;
    const unsigned char *image;
    size_t image_size;
} mapping_t;

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool free_malloc(void *p) {
    free(p);
    return true;
}

static const char *field(scenario_run_t *r, yyjson_val *obj, const char *name) {
    yyjson_val *v = yyjson_obj_get(obj, name);
    const char *s = yyjson_get_str(v);
    REQUIRE(s && strlen(s) == yyjson_get_len(v));
    return s;
}

static bool contains(const void *data, size_t size, const void *value, size_t n) {
    if (n > size)
        return false;
    for (size_t i = 0; i <= size - n; i++)
        if (!memcmp((const unsigned char *)data + i, value, n))
            return true;
    return false;
}

static void directory(scenario_run_t *r, const char *name) {
    fs_error_t e = {0};
    fs_info_t info;
    bool ok = create_fs_directory(name, &e);
    if (!ok && e.status == FS_EXISTS) {
        clear_fs_error(&e);
        ok = inspect_fs_path(name, false, &info, &e) && info.kind == FS_DIRECTORY;
    }
    clear_fs_error(&e);
    REQUIRE(ok);
}

/* Exceptional filenames are deliberately constructed only inside this project's
 * owned root. Never relax the shared runner's traversal/Windows path checks. */
static const char *compile(scenario_run_t *r, module_execution_project_t *p, yyjson_val *op) {
    const char *name = field(r, op, "name"), *version = field(r, op, "version");
    yyjson_val *files = yyjson_obj_get(op, "files"), *key, *value;
    REQUIRE(yyjson_is_obj(files));
    size_t i, total, count = 0;
    module_execution_file_t *regular =
        language_owned(r, CALLOC((yyjson_obj_size(files) + 1) * sizeof(*regular)));
    yyjson_obj_foreach(files, i, total, key, value) {
        const char *file = yyjson_get_str(key), *text = yyjson_get_str(value);
        REQUIRE(file && text && strlen(file) == yyjson_get_len(key)
                && strlen(text) == yyjson_get_len(value));
        const char *target = NULL;
        if (!strcmp(file, "../../outside-a/math.goat")
            || !strcmp(file, "../../outside-b/math.goat")) {
            const char *sub;
            /* The two exact fixture names above are the only parent-relative paths. */
            sub = !strcmp(file, "../../outside-a/math.goat") ? "outside-a" : "outside-b";
            const char *dir = language_join(r, module_execution_root(p), sub);
            directory(r, dir);
            target = language_join(r, dir, "math.goat");
        } else if (strchr(file, '\\')) {
#ifndef _WIN32
            REQUIRE(!strcmp(name, "app") && !strcmp(version, "1"));
            REQUIRE(!strcmp(file, "literal\\name\nnext\033\010\177\302\205.goat"));
            const char *sources = language_join(r, module_execution_root(p), "sources");
            directory(r, sources);
            const char *dir = language_join(r, sources, "app-1");
            directory(r, dir);
            target = language_join(r, dir, file);
#else
            REQUIRE(false);
#endif
        } else
            regular[count++] = (module_execution_file_t){file, text};
        if (target)
            language_write(r, target, text, strlen(text));
    }
    return module_execution_compile(p,
                                    name,
                                    field(r, op, "code"),
                                    version,
                                    regular,
                                    count,
                                    NULL,
                                    false);
}

static mapping_t mapping(scenario_run_t *r, const char *file) {
    size_t n;
    const unsigned char *zip = (const unsigned char *)language_read(r, file, &n);
    format_zip_info_t info;
    char error[256];
    REQUIRE(format_zip_inspect(zip, n, SIZE_MAX, &info, error, sizeof(error)));
    size_t count = info.count;
    mapping_t result = {0};
    for (size_t i = 0; i < count; i++) {
        REQUIRE(format_zip_inspect(zip, n, i, &info, error, sizeof(error)));
        bool metadata = info.name_size == 11 && !memcmp(info.name, "module.json", 11);
        bool image = info.name_size == 12 && !memcmp(info.name, "bytecode.bin", 12);
        if (!metadata && !image)
            continue;
        size_t size;
        unsigned char *data =
            format_zip_extract(zip, n, i, 16 * 1024 * 1024, &size, error, sizeof(error));
        REQUIRE(data);
        scenario_defer(r, free_malloc, data);
        if (metadata) {
            REQUIRE(!result.metadata);
            result.metadata = yyjson_read((const char *)data, size, 0);
            REQUIRE(result.metadata);
            scenario_defer(r, free_doc, result.metadata);
        } else {
            REQUIRE(!result.image);
            result.image = data;
            result.image_size = size;
        }
    }
    REQUIRE(result.metadata && result.image);
    const char *private = "PRIVATE_SOURCE_COMMENT_NOT_FOR_SHIPPING";
    REQUIRE(!contains(result.image, result.image_size, private, strlen(private)));
    return result;
}

static string_value_t unicode(scenario_run_t *r, const char *bytes, size_t size) {
    string_value_t value = decode_utf8_n(bytes, size);
    REQUIRE(value.data || !size);
    if (value.should_free)
        language_owned(r, (gchar_t *)value.data);
    size_t encoded_size;
    char *encoded = encode_utf8_view((string_view_t){value.data, value.length}, &encoded_size);
    REQUIRE(encoded);
    language_owned(r, encoded);
    REQUIRE(size == encoded_size && !memcmp(bytes, encoded, size));
    return value;
}

static const char *diagnostic_text(scenario_run_t *r,
                                   module_execution_project_t *p,
                                   scenario_command_result_t result,
                                   yyjson_val *checks) {
    string_value_t decoded = unicode(r, result.err, result.err_size);
    REQUIRE(decoded.length > 0);
    char *text = language_owned(r, ALLOC(result.err_size + 1));
    size_t n = 0;
    const char *raw = result.err;
    for (size_t i = 0; i < result.err_size; i++) {
        REQUIRE(raw[i] != 0);
        if (raw[i] == '\r' && i + 1 < result.err_size && raw[i + 1] == '\n')
            i++;
        text[n++] = raw[i];
    }
    text[n] = 0;
    REQUIRE(strstr(text, "Uncaught:"));
    const char *root = module_execution_root(p);
    REQUIRE(!strstr(text, root));
    char *slashes = language_owned(r, ALLOC(strlen(root) + 1));
    strcpy(slashes, root);
    for (char *s = slashes; *s; s++)
        if (*s == '\\')
            *s = '/';
    REQUIRE(!strstr(text, slashes));
    yyjson_val *fragments = yyjson_obj_get(checks, "fragments"), *v;
    size_t i, count;
    REQUIRE(yyjson_is_arr(fragments));
    yyjson_arr_foreach(fragments, i, count, v) {
        const char *part = yyjson_get_str(v);
        REQUIRE(part && strlen(part) == yyjson_get_len(v));
        scenario_require(r, strstr(text, part) != NULL, part);
    }
    return text;
}

static void remove_project(scenario_run_t *r, void *arg) {
    fs_error_t e = {0};
    bool ok = remove_fs_tree(arg, &e);
    clear_fs_error(&e);
    REQUIRE(ok);
    fs_info_t info;
    bool exists = inspect_fs_path(arg, false, &info, &e);
    bool absent = !exists && e.status == FS_NOT_FOUND;
    clear_fs_error(&e);
    REQUIRE(absent);
}

static void copy_file(scenario_run_t *r, const char *from, const char *to) {
    size_t size;
    const char *data = language_read(r, from, &size);
    language_write(r, to, data, size);
}

static void body(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *name = field(r, c->fixture, "name");
    module_execution_suite_t *suite = module_execution_suite(r, c->host);
    module_execution_project_t *p = module_execution_project(suite, name);
    yyjson_val *compiles = yyjson_obj_get(c->fixture, "compiles"), *op;
    size_t i, n;
    REQUIRE(yyjson_is_arr(compiles) && yyjson_arr_size(compiles));
    const char *archive = NULL;
    yyjson_arr_foreach(compiles, i, n, op) archive = compile(r, p, op);
    yyjson_val *checks = yyjson_arr_get(yyjson_obj_get(c->fixture, "diagnostics"), 0);
    REQUIRE(yyjson_is_obj(checks));
    bool frozen = !strcmp(name, "frozen");
    scenario_command_result_t result;
    if (frozen) {
        const char *args[] = {c->host, "frozen", archive, NULL};
        child_options_t options = {.executable = c->host,
                                   .arguments = args,
                                   .working_directory = module_execution_unrelated(suite),
                                   .environment = module_execution_environment(p)};
        result = scenario_command_barrier(r,
                                          &options,
#ifdef _WIN32
                                          "READY\r\n",
#else
                                          "READY\n",
#endif
                                          remove_project,
                                          (void *)module_execution_root(p));
        REQUIRE(result.exit_code != 0);
        scenario_require_bytes(r, "frozen stdout", "READY\n", 6, result.out, result.out_size, true);
    } else {
        const char *expected = field(r, checks, "expected");
        result =
            module_execution_invoke(p, "binary", archive, NULL, expected, strlen(expected), true);
    }
    const char *text = diagnostic_text(r, p, result, checks);
    if (!strncmp(name, "external-", 9)) {
        mapping_t m = mapping(r, archive);
        yyjson_val *files = yyjson_obj_get(yyjson_doc_get_root(m.metadata), "files");
        yyjson_val *expected = yyjson_obj_get(c->fixture, "mapping");
        REQUIRE(yyjson_is_arr(files) && yyjson_is_arr(expected)
                && yyjson_arr_size(files) == yyjson_arr_size(expected));
        for (size_t j = 0; j < yyjson_arr_size(files); j++) {
            yyjson_val *f = yyjson_arr_get(files, j), *e = yyjson_arr_get(expected, j);
            REQUIRE(yyjson_equals(yyjson_obj_get(f, "name"), yyjson_arr_get(e, 0)));
            REQUIRE(yyjson_equals(yyjson_obj_get(f, "external"), yyjson_arr_get(e, 1)));
        }
        const char *root = module_execution_root(p);
        REQUIRE(!contains(m.image, m.image_size, root, strlen(root)));
        string_value_t u = unicode(r, root, strlen(root));
        unsigned char *le = language_owned(r, ALLOC(u.length * 4));
        for (size_t j = 0; j < u.length; j++)
            for (unsigned k = 0; k < 4; k++)
                le[j * 4 + k] = (unsigned char)((uint32_t)u.data[j] >> (k * 8));
        REQUIRE(!contains(m.image, m.image_size, le, u.length * 4));
        if (!strcmp(name, "external-local"))
            REQUIRE(!strstr(text, "external file"));
    } else if (!strcmp(name, "relocation")) {
        mapping_t original = mapping(r, archive);
        module_execution_project_t *moved = module_execution_project(suite, "relocated");
        const char *old = language_join(r, module_execution_root(p), "sources/app-1");
        const char *sources = language_join(r, module_execution_root(moved), "sources");
        directory(r, sources);
        const char *dir = language_join(r, sources, "app-1");
        directory(r, dir);
        directory(r, language_join(r, dir, "lib"));
        const char *paths[] = {"module.gmod", "main.goat", "lib/vector.goat"};
        for (size_t j = 0; j < 3; j++)
            copy_file(r, language_join(r, old, paths[j]), language_join(r, dir, paths[j]));
        const char *out = language_join(r, module_execution_root(moved), "app.gbin");
        module_execution_invoke(moved,
                                "compile",
                                language_join(r, dir, "module.gmod"),
                                out,
                                "",
                                0,
                                false);
        mapping_t after = mapping(r, out);
        REQUIRE(yyjson_equals(yyjson_doc_get_root(original.metadata),
                              yyjson_doc_get_root(after.metadata)));
        scenario_require_bytes(r,
                               "relocated bytecode",
                               original.image,
                               original.image_size,
                               after.image,
                               after.image_size,
                               false);
        result = module_execution_invoke(moved, "binary", out, NULL, "", 0, true);
        const char *second = diagnostic_text(r, moved, result, checks);
        scenario_require_bytes(r,
                               "relocated diagnostic",
                               text,
                               strlen(text),
                               second,
                               strlen(second),
                               false);
    } else if (!strcmp(name, "imported")) {
        const char *first = strstr(text, "library@2:"), *second = strstr(text, "app@1:");
        REQUIRE(first && second && first < second);
    } else if (!strcmp(name, "anonymous")) {
        char *lower = language_owned(r, ALLOC(strlen(text) + 1));
        for (size_t j = 0; j <= strlen(text); j++)
            lower[j] = (char)tolower((unsigned char)text[j]);
        REQUIRE(strstr(lower, "anonymous") || strstr(lower, "<function")
                || strstr(lower, "function #"));
    } else if (!strcmp(name, "saved-failure"))
        REQUIRE(!strstr(text, "main.goat:4:"));
    else if (!strcmp(name, "escaped-label")) {
        REQUIRE(!strstr(text, "literal/name") && !strstr(text, "\nnext"));
        string_value_t u = unicode(r, text, strlen(text));
        for (size_t j = 0; j < u.length; j++) {
            uint32_t ch = (uint32_t)u.data[j];
            REQUIRE(!(ch < 32 && ch != '\n') && !(ch >= 127 && ch <= 159));
        }
    }
    printf("%s: %zu compile/VM checks passed\n",
           name,
           module_execution_count(suite) + (frozen ? 1 : 0));
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
    if (argc == 6 || argc == 7) {
        fs_error_t e = {0};
        char *host = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        char *filename = join_fs_path(argv[5], "module-source-diagnostics/scenarios.json", &e);
        clear_fs_error(&e);
        void *data = NULL;
        size_t size = 0;
        bool read = filename && read_fs_regular_file(filename, 1024 * 1024, &data, &size, &e);
        clear_fs_error(&e);
        yyjson_doc *doc = read ? yyjson_read(data, size, 0) : NULL;
        yyjson_val *array = yyjson_doc_get_root(doc);
        size_t count = yyjson_arr_size(array);
        if (host && yyjson_is_arr(array) && count) {
            configuration_t *c = CALLOC(count * sizeof(*c));
            scenario_definition_t *defs = CALLOC(count * sizeof(*defs));
            for (size_t i = 0; i < count; i++) {
                yyjson_val *v = yyjson_arr_get(array, i);
                c[i] = (configuration_t){host, v};
                defs[i] = (scenario_definition_t){yyjson_get_str(yyjson_obj_get(v, "id")),
                                                  120000,
                                                  body,
                                                  &c[i],
                                                  NULL};
#ifdef _WIN32
                if (!strcmp(yyjson_get_str(yyjson_obj_get(v, "name")), "escaped-label"))
                    defs[i].skip_reason = "Unix filename control characters/backslash are not "
                                          "valid Windows filenames";
#endif
            }
            scenario_options_t o = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(defs, count, argc == 7 ? 2 : 1, selection, &o);
            FREE(defs);
            FREE(c);
        } else
            fputs("Cannot load source diagnostic fixtures\n", stderr);
        yyjson_doc_free(doc);
        FREE(data);
        FREE(filename);
        FREE(host);
    } else
        fputs("usage: module_source_diagnostics_scenarios ID HOST BUILD DATA FIXTURES "
              "[--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
