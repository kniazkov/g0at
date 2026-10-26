/** @file module_consistency.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Immutable module cross-part and snapshot reference validation.
 */
#include "module_consistency.h"

#include "module/module_abi.h"
#include "module/module_json_internal.h"

#include <stdlib.h>
#include <string.h>

static const yyjson_val *root(const goat_module_container *container) {
    return yyjson_doc_get_root(goat_module_container_metadata(container));
}

static bool equal_text(const yyjson_val *a, const yyjson_val *b) {
    return yyjson_is_str(a) && yyjson_is_str(b) && yyjson_get_len(a) == yyjson_get_len(b)
           && !memcmp(yyjson_get_str(a), yyjson_get_str(b), yyjson_get_len(a));
}

module_consistency_status_t read_module_consistency(const goat_module_container *container,
                                                    bytecode_t **out,
                                                    uint32_t *entry,
                                                    module_consistency_error_t *error) {
    module_consistency_error_t result = {.status = MODULE_CONSISTENCY_ARGUMENT,
                                         .bytecode = {.index = SIZE_MAX},
                                         .native = {.variant_index = SIZE_MAX}};
    if (out)
        *out = NULL;
    if (entry)
        *entry = 0;
    if (!container || !out || !entry)
        goto finish;
    char *json = NULL;
    size_t size = 0;
    if (goat_module_json_write(goat_module_container_metadata(container),
                               GOAT_MODULE_METADATA,
                               false,
                               &json,
                               &size)) {
        result.status = MODULE_CONSISTENCY_JSON;
        goto finish;
    }
    zip_view_t image = goat_module_container_bytecode(container);
    bytecode_t *code = NULL;
    uint32_t start = 0;
    module_bytecode_status_t bytecode =
        read_module_bytecode(json, size, image.data, image.size, &code, &start, &result.bytecode);
    goat_module_json_free(json);
    if (bytecode) {
        result.status = MODULE_CONSISTENCY_BYTECODE;
        goto finish;
    }
    if (goat_module_native_validate(container, &result.native)) {
        free_bytecode(code);
        result.status = MODULE_CONSISTENCY_NATIVE;
        goto finish;
    }
    *out = code;
    *entry = start;
    result.status = MODULE_CONSISTENCY_OK;
finish:
    if (error)
        *error = result;
    return result.status;
}

/* Canonical comparisons preserve exact integer values and embedded zeros. */
static bool equal_value(const yyjson_val *a, const yyjson_val *b) {
    char *left = NULL, *right = NULL;
    size_t left_size = 0, right_size = 0;
    bool equal = !goat_json_canonical(a, &left, &left_size)
                 && !goat_json_canonical(b, &right, &right_size) && left_size == right_size
                 && !memcmp(left, right, left_size);
    goat_module_json_free(left);
    goat_module_json_free(right);
    return equal;
}

module_consistency_status_t match_module_descriptor_metadata(const yyjson_val *metadata,
                                                             const yyjson_val *variant,
                                                             const void *snapshot,
                                                             size_t size) {
    if (!metadata || !variant || !snapshot)
        return MODULE_CONSISTENCY_ARGUMENT;
    yyjson_doc *doc = NULL;
    if (goat_module_descriptor_read(snapshot, size, &doc))
        return MODULE_CONSISTENCY_DESCRIPTOR;
    const yyjson_val *descriptor = yyjson_doc_get_root(doc);
    bool equal =
        equal_text(yyjson_obj_get(metadata, "name"), yyjson_obj_get(descriptor, "module"))
        && equal_text(yyjson_obj_get(metadata, "version"), yyjson_obj_get(descriptor, "version"))
        && equal_text(yyjson_obj_get(metadata, "mainHash"), yyjson_obj_get(descriptor, "mainHash"));
    const char *fields[] = {"goatAbiVersion", "target", "specializations"};
    for (size_t i = 0; equal && i < sizeof(fields) / sizeof(*fields); i++)
        equal =
            equal_value(yyjson_obj_get(variant, fields[i]), yyjson_obj_get(descriptor, fields[i]));
    yyjson_doc_free(doc);
    return equal ? MODULE_CONSISTENCY_OK : MODULE_CONSISTENCY_DESCRIPTOR;
}

module_consistency_status_t match_module_descriptor(const goat_module_container *container,
                                                    size_t variant_index,
                                                    const void *snapshot,
                                                    size_t size) {
    if (!container || !snapshot)
        return MODULE_CONSISTENCY_ARGUMENT;
    const yyjson_val *metadata = root(container);
    const yyjson_val *variant = yyjson_arr_get(yyjson_obj_get(metadata, "native"), variant_index);
    return match_module_descriptor_metadata(metadata, variant, snapshot, size);
}

static bool identity(const yyjson_val *a, const yyjson_val *b, const char *name) {
    return equal_text(yyjson_obj_get(a, "name"), yyjson_obj_get(b, name))
           && equal_text(yyjson_obj_get(a, "version"), yyjson_obj_get(b, "version"));
}

module_consistency_status_t
check_module_references(const goat_module_container *const *nodes, size_t count, size_t owner) {
    if (!nodes || owner >= count || count > SIZE_MAX / sizeof(size_t))
        return MODULE_CONSISTENCY_ARGUMENT;
    for (size_t i = 0; i < count; i++) {
        if (!nodes[i])
            return MODULE_CONSISTENCY_ARGUMENT;
        for (size_t j = 0; j < i; j++)
            if (identity(root(nodes[i]), root(nodes[j]), "name"))
                return MODULE_CONSISTENCY_GRAPH;
    }
    bool *seen = calloc(count, sizeof(*seen));
    size_t *queue = malloc(count * sizeof(*queue));
    if (!seen || !queue) {
        free(seen);
        free(queue);
        return MODULE_CONSISTENCY_MEMORY;
    }
    module_consistency_status_t status = MODULE_CONSISTENCY_OK;
    size_t head = 0, tail = 1;
    queue[0] = owner;
    seen[owner] = true;
    while (head < tail) {
        const yyjson_val *dependencies = yyjson_obj_get(root(nodes[queue[head++]]), "dependencies");
        for (size_t i = 0; i < yyjson_arr_size(dependencies); i++) {
            const yyjson_val *edge = yyjson_arr_get(dependencies, i);
            size_t j = 0;
            while (j < count && !identity(root(nodes[j]), edge, "name"))
                j++;
            if (j == count
                || !equal_text(yyjson_obj_get(root(nodes[j]), "mainHash"),
                               yyjson_obj_get(edge, "hash"))) {
                status = MODULE_CONSISTENCY_GRAPH;
                goto finish;
            }
            if (!seen[j]) {
                seen[j] = true;
                queue[tail++] = j;
            }
        }
    }
    yyjson_obj_iter it = yyjson_obj_iter_with(yyjson_obj_get(root(nodes[owner]), "externals"));
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&it))) {
        const yyjson_val *ref = yyjson_obj_get(yyjson_obj_iter_get_val(key), "functionRef");
        if (!ref)
            continue;
        size_t j = 0;
        while (j < count && (j == owner || !seen[j] || !identity(root(nodes[j]), ref, "module")))
            j++;
        if (j == count) {
            status = MODULE_CONSISTENCY_REFERENCE;
            goto finish;
        }
        int64_t wanted = 0;
        goat_json_integer(yyjson_obj_get(ref, "functionId"), &wanted);
        const yyjson_val *functions = yyjson_obj_get(root(nodes[j]), "functions");
        bool found = false;
        for (size_t i = 0; i < yyjson_arr_size(functions); i++) {
            int64_t id = 0;
            goat_json_integer(yyjson_obj_get(yyjson_arr_get(functions, i), "functionId"), &id);
            if (id == wanted) {
                found = true;
                break;
            }
        }
        if (!found) {
            status = MODULE_CONSISTENCY_REFERENCE;
            goto finish;
        }
    }
finish:
    free(seen);
    free(queue);
    return status;
}
