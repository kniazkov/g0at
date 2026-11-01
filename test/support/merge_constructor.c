/** @file merge_constructor.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real compiler payload, independent archive builder, and no-load checks.
 */
#include "test/support/merge_constructor.h"

#include "test/support/format_binary.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"

#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)

typedef struct {
    const void *data;
    size_t size;
} bytes_t;

struct merge_constructor_t {
    scenario_run_t *run;
    const char *root, *input;
    bytes_t archive, code, payload;
};

static bool release(void *p) {
    free(p);
    return true;
}

static bool release_doc(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bytes_t read(scenario_run_t *r, const char *p) {
    size_t n;
    const char *d = language_read(r, p, &n);
    return (bytes_t){d, n};
}

static void *own(scenario_run_t *r, void *p) {
    REQUIRE(p);
    scenario_defer(r, release, p);
    return p;
}

static yyjson_mut_doc *parse(scenario_run_t *r, bytes_t b) {
    char error[256];
    yyjson_mut_doc *d = format_json_parse(b.data, b.size, error, sizeof(error));
    REQUIRE(d);
    scenario_defer(r, release_doc, d);
    return d;
}

static bytes_t blob(scenario_run_t *r, const char *data, yyjson_mut_val *record, const char *key) {
    const char *sha = yyjson_mut_get_str(yyjson_mut_obj_get(record, key));
    REQUIRE(sha && strlen(sha) == 64);
    bytes_t b = read(r, language_join(r, language_join(r, data, "blobs"), sha));
    char actual[65];
    format_binary_sha256(b.data, b.size, actual);
    REQUIRE(!strcmp(sha, actual));
    return b;
}

static void
set(scenario_run_t *r, yyjson_mut_doc *d, yyjson_mut_val *v, const char *k, yyjson_mut_val *x) {
    REQUIRE(x && yyjson_mut_obj_put(v, yyjson_mut_str(d, k), x));
}

static bytes_t extract(scenario_run_t *r, bytes_t archive, const char *name) {
    format_zip_info_t info;
    char error[256];
    REQUIRE(format_zip_inspect(archive.data, archive.size, SIZE_MAX, &info, error, sizeof(error)));
    size_t count = info.count;
    for (size_t i = 0; i < count; i++) {
        REQUIRE(format_zip_inspect(archive.data, archive.size, i, &info, error, sizeof(error)));
        if (info.name_size == strlen(name) && !memcmp(info.name, name, info.name_size)) {
            size_t n;
            void *p = own(r,
                          format_zip_extract(archive.data,
                                             archive.size,
                                             i,
                                             32 * 1024 * 1024,
                                             &n,
                                             error,
                                             sizeof(error)));
            return (bytes_t){p, n};
        }
    }
    REQUIRE(false);
    return (bytes_t){0};
}

merge_constructor_t *merge_constructor_prepare(scenario_run_t *r,
                                               const char *cc,
                                               const char *data,
                                               const char *root,
                                               const char *format,
                                               yyjson_mut_val *record) {
    merge_constructor_t *f = own(r, calloc(1, sizeof(*f)));
    f->run = r;
    f->root = root;
    f->input = language_join(r, root, "native-d");
    bytes_t source = blob(r, data, record, "constructor_source");
    const char *source_path = language_join(r, root, "constructor.c");
    language_write(r, source_path, source.data, source.size);
#ifdef _WIN32
    const char *library = language_join(r, root, "constructor.dll"),
               *member = "native/constructor.dll";
    const char *args[] = {cc, "-shared", source_path, "-o", library, NULL};
#else
    const char *library = language_join(r, root, "constructor.so"),
               *member = "native/constructor.so";
    const char *args[] = {cc, "-fPIC", "-shared", source_path, "-o", library, NULL};
#endif
    scenario_command_result_t result = language_command(r, cc, args, NULL);
    REQUIRE(result.exit_code == 0);
    f->payload = read(r, library);
    f->code = blob(r, data, record, "constructor_bytecode");
    yyjson_mut_doc *d = parse(r, blob(r, data, record, "constructor_metadata"));
    yyjson_mut_val *m = yyjson_mut_doc_get_root(d), *variants = yyjson_mut_obj_get(m, "native"),
                   *v = yyjson_mut_arr_get(variants, 0);
    REQUIRE(yyjson_mut_arr_size(variants) == 1 && v);
    char hash[65];
    format_binary_sha256(f->payload.data, f->payload.size, hash);
    set(r, d, v, "hash", yyjson_mut_strcpy(d, hash));
    set(r, d, v, "library", yyjson_mut_strcpy(d, member));
#ifdef _WIN32
    yyjson_mut_val *t = yyjson_mut_obj_get(v, "target");
    set(r, d, t, "os", yyjson_mut_str(d, "windows"));
    yyjson_mut_val *minimum = yyjson_mut_arr(d);
    REQUIRE(minimum && yyjson_mut_arr_add_uint(d, minimum, 10));
    set(r, d, t, "minimumOs", minimum);
    yyjson_mut_val *runtime = yyjson_mut_obj(d), *version = yyjson_mut_arr(d);
    REQUIRE(runtime && version && yyjson_mut_arr_add_uint(d, version, 0)
            && yyjson_mut_obj_add_str(d, runtime, "family", "ucrt")
            && yyjson_mut_obj_add_val(d, runtime, "minimumVersion", version));
    set(r, d, t, "runtime", runtime);
#endif
    yyjson_mut_doc *schema = parse(r, read(r, language_join(r, format, "schema-source.json")));
    char error[256];
    REQUIRE(format_binary_main_hash(m,
                                    yyjson_mut_doc_get_root(schema),
                                    f->code.data,
                                    f->code.size,
                                    hash,
                                    error,
                                    sizeof(error)));
    const char *supplied = yyjson_mut_get_str(yyjson_mut_obj_get(m, "mainHash"));
    REQUIRE(supplied && !strcmp(supplied, hash));
    size_t n;
    char *json = own(r, yyjson_mut_write(d, 0, &n));
    format_zip_entry_t entries[3];
    format_zip_entry_init(entries, "module.json", json, n, false);
    format_zip_entry_init(entries + 1, "bytecode.bin", f->code.data, f->code.size, false);
    format_zip_entry_init(entries + 2, member, f->payload.data, f->payload.size, false);
    void *bytes = own(r, format_zip_build(entries, 3, NULL, 0, false, &n, error, sizeof(error)));
    f->archive = (bytes_t){bytes, n};
    language_write(r, f->input, bytes, n);
    return f;
}

void merge_constructor_verify(merge_constructor_t *f) {
    scenario_run_t *r = f->run;
    bytes_t input = read(r, f->input);
    scenario_require_bytes(r,
                           "constructor input unchanged",
                           f->archive.data,
                           f->archive.size,
                           input.data,
                           input.size,
                           false);
    bytes_t output = read(r, language_join(r, f->root, "constructor-result"));
    yyjson_mut_doc *d = parse(r, extract(r, output, "module.json"));
    yyjson_mut_val *variants = yyjson_mut_obj_get(yyjson_mut_doc_get_root(d), "native");
    REQUIRE(yyjson_mut_arr_size(variants) == 1);
    const char *name =
        yyjson_mut_get_str(yyjson_mut_obj_get(yyjson_mut_arr_get(variants, 0), "library"));
    REQUIRE(name);
    bytes_t actual = extract(r, output, name);
    scenario_require_bytes(r,
                           "constructor native payload unchanged",
                           f->payload.data,
                           f->payload.size,
                           actual.data,
                           actual.size,
                           false);
    actual = extract(r, output, "bytecode.bin");
    scenario_require_bytes(r,
                           "constructor bytecode unchanged",
                           f->code.data,
                           f->code.size,
                           actual.data,
                           actual.size,
                           false);
    REQUIRE(!language_exists(r, language_join(r, f->root, "constructor-marker")));
}
