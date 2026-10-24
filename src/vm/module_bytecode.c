/** @file module_bytecode.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Cross-checks checked portable images and strict module metadata.
 */
#include "module_bytecode.h"

#include "goat/module_identity.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "lib/wire.h"
#include "module/module_json_internal.h"

#include <string.h>

static uint32_t number(const yyjson_val *object, const char *key) {
    int64_t value = 0;
    /* The strict metadata reader has already checked exact uint32 tokens. */
    goat_json_integer(yyjson_obj_get(object, key), &value);
    return (uint32_t)value;
}

static bool same_name(const gchar_t *text, size_t length, const yyjson_val *json) {
    string_value_t name = decode_utf8_n(yyjson_get_str(json), yyjson_get_len(json));
    bool equal =
        name.data && name.length == length && !memcmp(text, name.data, length * sizeof(gchar_t));
    FREE_STRING(name);
    return equal;
}

static module_bytecode_status_t
check_tables(const bytecode_t *code, const yyjson_val *root, size_t *index) {
    yyjson_val *files = yyjson_obj_get(root, "files");
    if (yyjson_arr_size(files) != code->file_count || number(root, "main") >= code->file_count)
        return MODULE_BYTECODE_FILES;
    for (size_t i = 0; i < code->file_count; ++i) {
        *index = i;
        if (number(yyjson_arr_get(files, i), "entry") != code->files[i].entry)
            return MODULE_BYTECODE_FILES;
    }
    *index = SIZE_MAX;
    yyjson_val *functions = yyjson_obj_get(root, "functions");
    if (yyjson_arr_size(functions) != code->function_count)
        return MODULE_BYTECODE_FUNCTIONS;
    /* Both checked tables are sorted by ID, not indexed by ID. Gaps are legal. */
    for (size_t i = 0; i < code->function_count; ++i) {
        *index = i;
        const bytecode_function_t *function = code->functions + i;
        yyjson_val *json = yyjson_arr_get(functions, i);
        yyjson_val *parameters = yyjson_obj_get(json, "parameters");
        if (number(json, "functionId") != function->id || number(json, "file") != function->file
            || number(json, "instruction") != function->instruction
            || yyjson_arr_size(parameters) != function->parameter_count)
            return MODULE_BYTECODE_FUNCTIONS;
        if (!function->parameter_count)
            continue;
        const data_descriptor_t *vector = code->data_descriptors + function->parameters;
        for (size_t j = 0; j < function->parameter_count; ++j) {
            uint32_t descriptor = read_le32(code->data + vector->offset + j * 4);
            const data_descriptor_t *name = code->data_descriptors + descriptor;
            if (!same_name((const gchar_t *)(code->data + name->offset),
                           name->count,
                           yyjson_obj_get(yyjson_arr_get(parameters, j), "name")))
                return MODULE_BYTECODE_PARAMETERS;
        }
    }
    *index = SIZE_MAX;
    return MODULE_BYTECODE_OK;
}

static module_bytecode_status_t
check_imports(const bytecode_t *code, const yyjson_val *root, size_t *index) {
    yyjson_val *dependencies = yyjson_obj_get(root, "dependencies");
    size_t count = yyjson_arr_size(dependencies);
    module_bytecode_status_t status = MODULE_BYTECODE_OK;
    for (size_t i = 0; i < code->instructions_count; ++i) {
        instruction_t instruction = code->instructions[i];
        if (instruction.opcode != MODULE_IMPORT)
            continue;
        *index = i;
        const data_descriptor_t *descriptor = code->data_descriptors + instruction.arg1;
        goat_module_import_wide_t spec;
        if (!goat_module_parse_import_wide((const gchar_t *)(code->data + descriptor->offset),
                                           descriptor->count,
                                           &spec)
            || !spec.has_version) {
            status = MODULE_BYTECODE_IMPORTS;
            break;
        }
        size_t j;
        for (j = 0; j < count; ++j) {
            yyjson_val *dependency = yyjson_arr_get(dependencies, j);
            if (same_name(spec.name.data, spec.name.length, yyjson_obj_get(dependency, "name"))
                && same_name(spec.version.data,
                             spec.version.length,
                             yyjson_obj_get(dependency, "version"))) {
                break;
            }
        }
        if (j == count) {
            status = MODULE_BYTECODE_IMPORTS;
            break;
        }
    }
    if (!status)
        *index = SIZE_MAX;
    return status;
}

module_bytecode_status_t read_module_bytecode(const char *metadata,
                                              size_t metadata_size,
                                              const void *image,
                                              size_t image_size,
                                              bytecode_t **out,
                                              uint32_t *entry,
                                              module_bytecode_error_t *error) {
    module_bytecode_error_t result = {MODULE_BYTECODE_ARGUMENT, SIZE_MAX, GOAT_MODULE_JSON_OK};
    if (out)
        *out = NULL;
    if (entry)
        *entry = 0;
    if (!out || !entry || !metadata || !image)
        goto finish;
    yyjson_doc *doc = NULL;
    result.json_error = goat_module_json_read(metadata, metadata_size, GOAT_MODULE_METADATA, &doc);
    if (result.json_error) {
        result.status = MODULE_BYTECODE_JSON;
        goto finish;
    }
    bytecode_t *code = decode_bytecode_image(image, image_size);
    if (!code) {
        yyjson_doc_free(doc);
        result.status = MODULE_BYTECODE_IMAGE;
        goto finish;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    result.status = check_tables(code, root, &result.index);
    if (!result.status)
        result.status = check_imports(code, root, &result.index);
    if (!result.status) {
        *entry = code->files[number(root, "main")].entry;
        *out = code;
    } else {
        free_bytecode(code);
    }
    yyjson_doc_free(doc);
finish:
    if (error)
        *error = result;
    return result.status;
}

const char *module_bytecode_status_string(module_bytecode_status_t status) {
    static const char *const names[] = {"ok",
                                        "invalid argument",
                                        "invalid module JSON",
                                        "invalid bytecode image",
                                        "file table mismatch",
                                        "function table mismatch",
                                        "parameter name mismatch",
                                        "import/dependency mismatch"};
    return (unsigned)status < sizeof(names) / sizeof(names[0]) ? names[status] : "unknown status";
}
