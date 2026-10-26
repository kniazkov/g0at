/** @file module_native_merge.c
 * @brief Native variant deduplication and compatibility conflict checks.
 */
#include "module_native_merge.h"

#include "module_json_internal.h"
#include "module_native.h"

#include <goat/module_abi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const yyjson_val *variant, *compiler;
    char *key, *specializations;
    size_t key_length, specializations_length;
    zip_view_t bytes;
} record;

static bool same_text(const yyjson_val *a, const yyjson_val *b) {
    return a && b && yyjson_get_len(a) == yyjson_get_len(b)
           && !memcmp(yyjson_get_str(a), yyjson_get_str(b), yyjson_get_len(a));
}

static uint32_t abi(const yyjson_val *variant) {
    int64_t n = 0;
    goat_json_integer(yyjson_obj_get(variant, "goatAbiVersion"), &n);
    return (uint32_t)n;
}

static int compare(const void *a, const void *b) {
    const record *x = a, *y = b;
    size_t length = x->key_length < y->key_length ? x->key_length : y->key_length;
    int result = memcmp(x->key, y->key, length);
    if (!result)
        result = (x->key_length > y->key_length) - (x->key_length < y->key_length);
    uint32_t xa = abi(x->variant), ya = abi(y->variant);
    return result ? result : (xa > ya) - (xa < ya);
}

static goat_native_merge_status overlaps(const yyjson_val *a, const yyjson_val *b, bool *out) {
    *out = false;
    yyjson_val *target = yyjson_obj_get(a, "target");
    char *json = NULL;
    size_t length = 0;
    /* This helper is called only after allocating canonical requirement keys.
     * Its allocation failure must not accidentally authorize publication. */
    if (goat_json_canonical(target, &json, &length))
        return GOAT_NATIVE_MERGE_MEMORY;
    goat_native_environment environment;
    goat_module_json_error error = goat_native_environment_read(json, length, &environment);
    goat_module_json_free(json);
    if (error)
        return error == GOAT_MODULE_JSON_MEMORY ? GOAT_NATIVE_MERGE_MEMORY
                                                : GOAT_NATIVE_MERGE_ARGUMENT;
    environment.os_version.known = environment.runtime_version.known = true;
    for (size_t i = 0; i < 4; i++) {
        environment.os_version.components[i] = UINT32_MAX;
        environment.runtime_version.components[i] = UINT32_MAX;
    }
    environment.cpu_features = UINT64_MAX;
    /* Only implemented ABIs can establish an actual compatibility witness.
     * Unknown versions remain preserved but unselectable, like unknown CPU names. */
    uint32_t supported[] = {GOAT_MODULE_ABI_VERSION};
    goat_native_compatibility_result result;
    *out =
        goat_module_native_compatibility(a, &environment, supported, 1, &result) == GOAT_NATIVE_OK
        && goat_module_native_compatibility(b, &environment, supported, 1, &result)
               == GOAT_NATIVE_OK;
    return GOAT_NATIVE_MERGE_OK;
}

void goat_native_merge_clear(goat_native_merge_result *result) {
    if (!result)
        return;
    yyjson_doc_free(result->metadata);
    free(result->payloads);
    memset(result, 0, sizeof(*result));
}

goat_native_merge_status goat_native_merge(const goat_module_container *const *inputs,
                                           size_t count,
                                           goat_native_merge_result *out) {
    if (!out)
        return GOAT_NATIVE_MERGE_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!inputs || !count)
        return GOAT_NATIVE_MERGE_ARGUMENT;
    record *records = NULL;
    size_t used = 0;
    goat_native_merge_status status = GOAT_NATIVE_MERGE_OK;
    yyjson_mut_doc *doc = NULL;
    for (size_t i = 0; i < count && !status; i++) {
        if (!inputs[i]) {
            status = GOAT_NATIVE_MERGE_ARGUMENT;
            break;
        }
        const yyjson_doc *metadata = goat_module_container_metadata(inputs[i]);
        yyjson_val *variants = yyjson_obj_get(yyjson_doc_get_root(metadata), "native");
        for (size_t j = 0; j < yyjson_arr_size(variants) && !status; j++) {
            const yyjson_val *variant = yyjson_arr_get(variants, j);
            record item = {.variant = variant, .compiler = yyjson_obj_get(variant, "cCompiler")};
            if (goat_json_canonical(yyjson_obj_get(variant, "target"), &item.key, &item.key_length)
                || goat_json_canonical(yyjson_obj_get(variant, "specializations"),
                                       &item.specializations,
                                       &item.specializations_length)) {
                free(item.key);
                free(item.specializations);
                status = GOAT_NATIVE_MERGE_MEMORY;
                break;
            }
            size_t k;
            for (k = 0; k < used; k++) {
                record *prior = &records[k];
                if (compare(prior, &item) || abi(prior->variant) != abi(variant))
                    continue;
                if (!same_text(yyjson_obj_get(prior->variant, "hash"),
                               yyjson_obj_get(variant, "hash"))
                    || prior->specializations_length != item.specializations_length
                    || memcmp(prior->specializations,
                              item.specializations,
                              item.specializations_length))
                    status = GOAT_NATIVE_MERGE_CONFLICT;
                else if (prior->compiler && item.compiler
                         && (!same_text(yyjson_obj_get(prior->compiler, "name"),
                                        yyjson_obj_get(item.compiler, "name"))
                             || !same_text(yyjson_obj_get(prior->compiler, "version"),
                                           yyjson_obj_get(item.compiler, "version"))))
                    status = GOAT_NATIVE_MERGE_COMPILER;
                else if (!prior->compiler)
                    prior->compiler = item.compiler;
                break;
            }
            if (k < used) {
                free(item.key);
                free(item.specializations);
                continue;
            }
            bool found = false;
            const yyjson_val *name = yyjson_obj_get(variant, "library");
            for (size_t p = 0; p < goat_module_container_native_count(inputs[i]); p++) {
                goat_container_native payload;
                if (goat_module_container_native_at(inputs[i], p, &payload)
                    && payload.name_length == yyjson_get_len(name)
                    && !memcmp(payload.name, yyjson_get_str(name), payload.name_length)) {
                    item.bytes = payload.bytes;
                    found = true;
                    break;
                }
            }
            record *next = found && used < SIZE_MAX / sizeof(*records)
                               ? realloc(records, (used + 1) * sizeof(*records))
                               : NULL;
            if (!next) {
                free(item.key);
                free(item.specializations);
                status = found ? GOAT_NATIVE_MERGE_MEMORY : GOAT_NATIVE_MERGE_ARGUMENT;
                break;
            }
            records = next;
            records[used++] = item;
        }
    }
    if (status)
        goto done;
    if (used > 1)
        qsort(records, used, sizeof(*records), compare);
    for (size_t i = 0; i < used; i++)
        for (size_t j = 0; j < i; j++) {
            bool overlap;
            status = overlaps(records[i].variant, records[j].variant, &overlap);
            if (status)
                goto done;
            if (overlap) {
                status = GOAT_NATIVE_MERGE_AMBIGUOUS;
                goto done;
            }
        }
    doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *array = doc ? yyjson_mut_arr(doc) : NULL;
    out->payloads = used ? calloc(used, sizeof(*out->payloads)) : NULL;
    if (!array || (used && !out->payloads)) {
        status = GOAT_NATIVE_MERGE_MEMORY;
        goto done;
    }
    yyjson_mut_doc_set_root(doc, array);
    for (size_t i = 0; i < used; i++) {
        char name[80];
        const yyjson_val *old_name = yyjson_obj_get(records[i].variant, "library");
        const char *suffix = yyjson_get_str(old_name) + yyjson_get_len(old_name) - 3;
        snprintf(name,
                 sizeof(name),
                 "native/merge-%zu.%s",
                 i,
                 !strcmp(suffix, "dll") ? "dll" : "so");
        yyjson_mut_val *copy = yyjson_val_mut_copy(doc, records[i].variant);
        if (!copy || !yyjson_mut_arr_append(array, copy)
            || !yyjson_mut_obj_put(copy,
                                   yyjson_mut_str(doc, "library"),
                                   yyjson_mut_strcpy(doc, name))
            || (records[i].compiler
                && !yyjson_mut_obj_put(copy,
                                       yyjson_mut_str(doc, "cCompiler"),
                                       yyjson_val_mut_copy(doc, records[i].compiler)))) {
            status = GOAT_NATIVE_MERGE_MEMORY;
            goto done;
        }
        out->payloads[i].bytes = records[i].bytes;
    }
    out->metadata = yyjson_mut_doc_imut_copy(doc, NULL);
    if (!out->metadata) {
        status = GOAT_NATIVE_MERGE_MEMORY;
        goto done;
    }
    out->count = used;
    for (size_t i = 0; i < used; i++) {
        yyjson_val *name =
            yyjson_obj_get(yyjson_arr_get(yyjson_doc_get_root(out->metadata), i), "library");
        out->payloads[i].name = yyjson_get_str(name);
        out->payloads[i].name_length = yyjson_get_len(name);
    }
done:
    yyjson_mut_doc_free(doc);
    for (size_t i = 0; i < used; i++) {
        free(records[i].key);
        free(records[i].specializations);
    }
    free(records);
    if (status)
        goat_native_merge_clear(out);
    return status;
}

const char *goat_native_merge_status_string(goat_native_merge_status status) {
    switch (status) {
        case GOAT_NATIVE_MERGE_OK:
            return "success";
        case GOAT_NATIVE_MERGE_ARGUMENT:
            return "invalid native merge input";
        case GOAT_NATIVE_MERGE_MEMORY:
            return "native merge allocation failure";
        case GOAT_NATIVE_MERGE_CONFLICT:
            return "conflicting native variants with identical requirements";
        case GOAT_NATIVE_MERGE_COMPILER:
            return "conflicting native C compiler information";
        case GOAT_NATIVE_MERGE_AMBIGUOUS:
            return "ambiguous compatible native variants";
    }
    return "unknown native merge error";
}
