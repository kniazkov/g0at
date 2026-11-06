/** @file module_artifact.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent ZIP fixtures for CLI bytecode and native integrity tests.
 */
#include "module_artifact.h"

#include "format_binary.h"
#include "format_zip.h"

#include <stdlib.h>
#include <string.h>

static bool release(void *p) {
    free(p);
    return true;
}

static bool release_doc(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static uint32_t word(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

const unsigned char *test_module_member(scenario_run_t *run,
                                        const void *zip,
                                        size_t size,
                                        const char *name,
                                        size_t *length) {
    format_zip_info_t info;
    char error[256] = {0};
    scenario_require(run,
                     format_zip_inspect(zip, size, SIZE_MAX, &info, error, sizeof(error)),
                     error);
    size_t count = info.count;
    for (size_t i = 0; i < count; ++i) {
        scenario_require(run, format_zip_inspect(zip, size, i, &info, error, sizeof(error)), error);
        if (info.name_size == strlen(name) && !memcmp(info.name, name, info.name_size)) {
            void *bytes = format_zip_extract(zip, size, i, SIZE_MAX, length, error, sizeof(error));
            scenario_require(run, bytes != NULL, error);
            scenario_defer(run, release, bytes);
            return bytes;
        }
    }
    scenario_require(run, false, "missing ZIP member");
    return NULL;
}

const unsigned char *
test_module_wrap_image(scenario_run_t *run, const void *image, size_t size, size_t *length) {
    char error[256] = {0};
    const unsigned char *bytes = image;
    const char initial[] = "{\"formatVersion\":1,\"bytecodeVersion\":1,\"name\":\"fixture\","
                           "\"version\":\"0\",\"main\":0,\"files\":[{\"name\":\"fixture.goat\","
                           "\"entry\":0,\"external\":false}],\"functions\":[],\"mainHash\":"
                           "\"0000000000000000000000000000000000000000000000000000000000000000\"}";
    yyjson_mut_doc *doc = format_json_parse(initial, sizeof(initial) - 1, error, sizeof(error));
    scenario_require(run, doc != NULL, error);
    scenario_defer(run, release_doc, doc);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    if (format_binary_check(image, size, error, sizeof(error))) {
        yyjson_mut_val *files = yyjson_mut_obj_get(root, "files");
        yyjson_mut_obj_put(yyjson_mut_arr_get(files, 0),
                           yyjson_mut_str(doc, "entry"),
                           yyjson_mut_uint(doc, word(bytes + word(bytes + 48) + 8)));
        yyjson_mut_val *functions = yyjson_mut_obj_get(root, "functions");
        for (uint32_t i = 0; i < word(bytes + 32); ++i) {
            const unsigned char *f = bytes + word(bytes + 52) + i * 32;
            yyjson_mut_val *record = yyjson_mut_obj(doc), *params = yyjson_mut_arr(doc);
            yyjson_mut_obj_add_uint(doc, record, "functionId", word(f));
            yyjson_mut_obj_add_uint(doc, record, "file", 0);
            yyjson_mut_obj_add_uint(doc, record, "instruction", word(f + 8));
            yyjson_mut_obj_add_val(doc, record, "parameters", params);
            for (uint32_t j = 0; j < word(f + 20) && j < size / 4; ++j) {
                /* Parameter names are informational; numeric fixture uses x. */
                yyjson_mut_val *p = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_str(doc, p, "name", "x");
                yyjson_mut_arr_append(params, p);
            }
            yyjson_mut_arr_append(functions, record);
        }
    }
    size_t schema_size;
    const char *schema_text = language_read(run, GOAT_TEST_SCHEMA, &schema_size);
    yyjson_mut_doc *schema = format_json_parse(schema_text, schema_size, error, sizeof(error));
    scenario_require(run, schema != NULL, error);
    scenario_defer(run, release_doc, schema);
    char hash[65];
    scenario_require(run,
                     format_binary_main_hash(root,
                                             yyjson_mut_doc_get_root(schema),
                                             image,
                                             size,
                                             hash,
                                             error,
                                             sizeof(error)),
                     error);
    yyjson_mut_obj_put(root, yyjson_mut_str(doc, "mainHash"), yyjson_mut_strcpy(doc, hash));
    size_t json_size;
    char *json = yyjson_mut_write(doc, 0, &json_size);
    scenario_require(run, json != NULL, "fixture metadata allocation");
    scenario_defer(run, release, json);
    format_zip_entry_t entries[2];
    format_zip_entry_init(entries, "module.json", json, json_size, true);
    format_zip_entry_init(entries + 1, "bytecode.bin", image, size, true);
    unsigned char *zip = format_zip_build(entries, 2, NULL, 0, false, length, error, sizeof(error));
    scenario_require(run, zip != NULL, error);
    scenario_defer(run, release, zip);
    return zip;
}

const unsigned char *test_module_replace(scenario_run_t *run,
                                         const void *zip,
                                         size_t size,
                                         const char *name,
                                         const void *data,
                                         size_t data_size,
                                         bool native_hash,
                                         size_t *length) {
    format_zip_info_t info;
    char error[256] = {0};
    scenario_require(run,
                     format_zip_inspect(zip, size, SIZE_MAX, &info, error, sizeof(error)),
                     error);
    size_t count = info.count;
    format_zip_entry_t *entries = calloc(count, sizeof(*entries));
    scenario_require(run, entries != NULL, "fixture entries allocation");
    scenario_defer(run, release, entries);
    bool replaced = false;
    for (size_t i = 0; i < count; ++i) {
        scenario_require(run, format_zip_inspect(zip, size, i, &info, error, sizeof(error)), error);
        char *key = malloc(info.name_size + 1);
        scenario_require(run, key != NULL, "fixture name allocation");
        memcpy(key, info.name, info.name_size);
        key[info.name_size] = 0;
        scenario_defer(run, release, key);
        size_t n;
        const void *part = test_module_member(run, zip, size, key, &n);
        if (!strcmp(key, name)) {
            part = data;
            n = data_size;
            replaced = true;
        }
        if (native_hash && !strcmp(key, "module.json")) {
            yyjson_mut_doc *doc = format_json_parse(part, n, error, sizeof(error));
            scenario_require(run, doc != NULL, error);
            scenario_defer(run, release_doc, doc);
            yyjson_mut_val *variants = yyjson_mut_obj_get(yyjson_mut_doc_get_root(doc), "native");
            char hash[65];
            format_binary_sha256(data, data_size, hash);
            bool found = false;
            for (size_t j = 0; j < yyjson_mut_arr_size(variants); ++j) {
                yyjson_mut_val *variant = yyjson_mut_arr_get(variants, j);
                if (!strcmp(yyjson_mut_get_str(yyjson_mut_obj_get(variant, "library")), name)) {
                    yyjson_mut_obj_put(variant,
                                       yyjson_mut_str(doc, "hash"),
                                       yyjson_mut_strcpy(doc, hash));
                    found = true;
                }
            }
            scenario_require(run, found, "native replacement declaration");
            part = yyjson_mut_write(doc, 0, &n);
            scenario_require(run, part != NULL, "fixture JSON allocation");
            scenario_defer(run, release, (void *)part);
        }
        format_zip_entry_init(entries + i, key, part, n, true);
    }
    scenario_require(run, replaced, "missing replacement member");
    unsigned char *result =
        format_zip_build(entries, count, NULL, 0, false, length, error, sizeof(error));
    scenario_require(run, result != NULL, error);
    scenario_defer(run, release, result);
    return result;
}
