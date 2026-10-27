/** @file module_dependencies.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Consumer-local exact dependency binding, without transitive flattening.
 */
#include "module_dependencies.h"

#include "goat/module_identity.h"
#include "lib/memory/allocate.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct module_dependencies {
    validated_module_t **snapshots;
    size_t count;
    yyjson_doc *metadata;
};

typedef struct {
    const char *name, *version, *hash;
} request_t;

void clear_module_dependencies_error(module_dependencies_error_t *error) {
    if (!error)
        return;
    clear_module_search_error(&error->search);
    memset(error, 0, sizeof(*error));
    error->index = SIZE_MAX;
}

void free_module_dependencies(module_dependencies_t *dependencies) {
    if (!dependencies)
        return;
    for (size_t i = 0; i < dependencies->count; i++)
        free_validated_module(dependencies->snapshots[i]);
    FREE(dependencies->snapshots);
    yyjson_doc_free(dependencies->metadata);
    FREE(dependencies);
}

size_t module_dependencies_count(const module_dependencies_t *dependencies) {
    return dependencies ? dependencies->count : 0;
}

const validated_module_t *module_dependency_snapshot(const module_dependencies_t *dependencies,
                                                     size_t index) {
    return dependencies && index < dependencies->count ? dependencies->snapshots[index] : NULL;
}

const yyjson_doc *module_dependencies_metadata(const module_dependencies_t *dependencies) {
    return dependencies ? dependencies->metadata : NULL;
}

static module_dependencies_status_t fail(module_dependencies_error_t *error,
                                         module_dependencies_status_t status) {
    if (error)
        error->status = status;
    return status;
}

static int compare(const void *a, const void *b) {
    const request_t *left = a, *right = b;
    int value = strcmp(left->name, right->name);
    return value ? value : strcmp(left->version, right->version);
}

static module_dependencies_status_t bind(request_t *requests,
                                         size_t count,
                                         const module_search_t *search,
                                         const module_validation_options_t *options,
                                         module_dependencies_t **out,
                                         module_dependencies_error_t *error) {
    if (count > 1)
        qsort(requests, count, sizeof(*requests), compare);
    for (size_t i = 1; i < count; i++)
        if (!compare(&requests[i - 1], &requests[i]))
            return fail(error, MODULE_DEPENDENCIES_ARGUMENT);
    module_dependencies_t *result = CALLOC(sizeof(*result));
    result->snapshots = CALLOC(count * sizeof(*result->snapshots));
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *array = doc ? yyjson_mut_arr(doc) : NULL;
    module_dependencies_status_t status = MODULE_DEPENDENCIES_MEMORY;
    if (!array)
        goto done;
    yyjson_mut_doc_set_root(doc, array);
    for (size_t i = 0; i < count; i++) {
        if (find_module(search,
                        requests[i].name,
                        requests[i].version,
                        requests[i].hash,
                        options,
                        &result->snapshots[i],
                        error ? &error->search : NULL)
            != MODULE_SEARCH_OK) {
            if (error)
                error->index = i;
            status = MODULE_DEPENDENCIES_SEARCH;
            goto done;
        }
        result->count++;
        const yyjson_doc *metadata =
            goat_module_container_metadata(validated_module_container(result->snapshots[i]));
        const char *hash =
            yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(metadata), "mainHash"));
        yyjson_mut_val *record = yyjson_mut_obj(doc);
        if (!record || !yyjson_mut_obj_add_strcpy(doc, record, "name", requests[i].name)
            || !yyjson_mut_obj_add_strcpy(doc, record, "version", requests[i].version)
            || !yyjson_mut_obj_add_strcpy(doc, record, "hash", hash)
            || !yyjson_mut_arr_append(array, record))
            goto done;
    }
    result->metadata = yyjson_mut_doc_imut_copy(doc, NULL);
    if (!result->metadata)
        goto done;
    *out = result;
    result = NULL;
    status = MODULE_DEPENDENCIES_OK;
done:
    yyjson_mut_doc_free(doc);
    free_module_dependencies(result);
    return fail(error, status);
}

module_dependencies_status_t bind_source_dependencies(const module_imports_t *imports,
                                                      const module_search_t *search,
                                                      const module_validation_options_t *options,
                                                      module_dependencies_t **out,
                                                      module_dependencies_error_t *error) {
    clear_module_dependencies_error(error);
    if (out)
        *out = NULL;
    if (!imports || !search || !out || (imports->count && !imports->dependencies)
        || imports->count > SIZE_MAX / sizeof(request_t)
        || imports->count > SIZE_MAX / sizeof(validated_module_t *))
        return fail(error, MODULE_DEPENDENCIES_ARGUMENT);
    request_t *requests = ALLOC(imports->count * sizeof(*requests));
    for (size_t i = 0; i < imports->count; i++) {
        const module_import_dependency_t *dep = &imports->dependencies[i];
        if (!dep->name || !dep->version || !goat_module_name_valid(dep->name, strlen(dep->name))
            || !goat_module_version_valid(dep->version, strlen(dep->version))) {
            FREE(requests);
            return fail(error, MODULE_DEPENDENCIES_ARGUMENT);
        }
        requests[i] = (request_t){dep->name, dep->version, NULL};
    }
    module_dependencies_status_t status =
        bind(requests, imports->count, search, options, out, error);
    FREE(requests);
    return status;
}

module_dependencies_status_t bind_module_dependencies(const validated_module_t *consumer,
                                                      const module_search_t *search,
                                                      const module_validation_options_t *options,
                                                      module_dependencies_t **out,
                                                      module_dependencies_error_t *error) {
    clear_module_dependencies_error(error);
    if (out)
        *out = NULL;
    if (!consumer || !search || !out)
        return fail(error, MODULE_DEPENDENCIES_ARGUMENT);
    const yyjson_doc *metadata =
        goat_module_container_metadata(validated_module_container(consumer));
    yyjson_val *array = yyjson_obj_get(yyjson_doc_get_root(metadata), "dependencies");
    size_t count = yyjson_arr_size(array);
    if (count > SIZE_MAX / sizeof(request_t) || count > SIZE_MAX / sizeof(validated_module_t *))
        return fail(error, MODULE_DEPENDENCIES_ARGUMENT);
    request_t *requests = ALLOC(count * sizeof(*requests));
    for (size_t i = 0; i < count; i++) {
        yyjson_val *record = yyjson_arr_get(array, i);
        requests[i] = (request_t){yyjson_get_str(yyjson_obj_get(record, "name")),
                                  yyjson_get_str(yyjson_obj_get(record, "version")),
                                  yyjson_get_str(yyjson_obj_get(record, "hash"))};
    }
    module_dependencies_status_t status = bind(requests, count, search, options, out, error);
    FREE(requests);
    return status;
}
