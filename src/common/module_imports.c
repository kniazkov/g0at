/** @file module_imports.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Static exact-version imports, independent of module lookup and execution.
 */
#include "module_imports.h"

#include "goat/module_identity.h"
#include "lib/allocate.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static char *copy_slice(const char *data, size_t length) {
    char *out = ALLOC(length + 1);
    memcpy(out, data, length);
    out[length] = 0;
    return out;
}

static void clear_dependencies(module_import_dependency_t *items, size_t count) {
    for (size_t i = 0; i < count; i++) {
        FREE(items[i].name);
        FREE(items[i].version);
        FREE(items[i].sites);
    }
    FREE(items);
}

void free_module_imports(module_imports_t *imports) {
    if (imports) {
        clear_dependencies(imports->dependencies, imports->count);
        clear_dependencies(imports->unused_defaults, imports->unused_count);
        FREE(imports);
    }
}

static module_imports_status_t fail(module_imports_error_t *error,
                                    module_imports_status_t status,
                                    module_import_site_t site,
                                    const char *message) {
    if (error) {
        error->status = status;
        error->site = site;
        error->message = message;
    }
    return status;
}

typedef struct {
    char *name, *version;
    module_import_site_t site;
} import_occurrence_t;

static int compare_occurrence(const void *left, const void *right) {
    const import_occurrence_t *a = left, *b = right;
    int cmp = strcmp(a->name, b->name);
    if (!cmp)
        cmp = strcmp(a->version, b->version);
    if (!cmp && a->site.file != b->site.file)
        cmp = a->site.file < b->site.file ? -1 : 1;
    if (!cmp && a->site.offset != b->site.offset)
        cmp = a->site.offset < b->site.offset ? -1 : 1;
    return cmp;
}

static bool same_pair(const import_occurrence_t *a, const import_occurrence_t *b) {
    return !strcmp(a->name, b->name) && !strcmp(a->version, b->version);
}

/* Defaults are indexed by name once, rather than rescanning the JSON object for
 * every unversioned occurrence. These records borrow the validated descriptor. */
static const char *default_version(const module_import_dependency_t *defaults,
                                   size_t count,
                                   goat_module_slice_t name) {
    size_t low = 0, high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        size_t length = strlen(defaults[middle].name);
        size_t common = length < name.length ? length : name.length;
        int cmp = memcmp(defaults[middle].name, name.data, common);
        if (!cmp && length != name.length)
            cmp = length < name.length ? -1 : 1;
        if (!cmp)
            return defaults[middle].version;
        if (cmp < 0)
            low = middle + 1;
        else
            high = middle;
    }
    return NULL;
}

static int compare_dependency(const void *left, const void *right) {
    const module_import_dependency_t *a = left, *b = right;
    int cmp = strcmp(a->name, b->name);
    return cmp ? cmp : strcmp(a->version, b->version);
}

module_imports_status_t module_imports_collect(const module_sources_t *sources,
                                               size_t max_imports,
                                               module_imports_t **out,
                                               module_imports_error_t *error) {
    if (error)
        *error = (module_imports_error_t){0};
    if (out)
        *out = NULL;
    module_import_site_t site = {0};
    if (!out || !sources || !sources->count || !sources->files)
        return fail(error, MODULE_IMPORTS_ARGUMENT, site, "invalid source graph");
    if (!max_imports)
        max_imports = 65536;
    yyjson_doc *descriptor = NULL;
    if (sources->descriptor) {
        char *bytes = NULL;
        size_t length = 0;
        goat_module_json_error code =
            goat_module_json_write(sources->descriptor, GOAT_MODULE_GMOD, false, &bytes, &length);
        if (!code)
            code = goat_module_json_read(bytes, length, GOAT_MODULE_GMOD, &descriptor);
        goat_module_json_free(bytes);
        if (code) {
            if (error)
                error->json = code;
            return fail(error, MODULE_IMPORTS_DESCRIPTOR, site, "invalid module descriptor");
        }
    }
    yyjson_val *defaults =
        descriptor ? yyjson_obj_get(yyjson_doc_get_root(descriptor), "dependencies") : NULL;
    module_imports_t *result = ALLOC(sizeof(*result));
    *result = (module_imports_t){0};
    module_imports_status_t status = MODULE_IMPORTS_OK;
    source_includes_t literals = {0};
    import_occurrence_t *records = NULL;
    size_t occurrences = 0, capacity = 0;
    size_t default_count = yyjson_obj_size(defaults);
    module_import_dependency_t *default_index = NULL;
    if (default_count > SIZE_MAX / sizeof(*default_index)) {
        status = fail(error, MODULE_IMPORTS_LIMIT, site, "too many default versions");
        goto done;
    }
    if (default_count) {
        default_index = ALLOC(default_count * sizeof(*default_index));
        size_t index, maximum;
        yyjson_val *key, *value;
        yyjson_obj_foreach(defaults, index, maximum, key, value) {
            default_index[index] = (module_import_dependency_t){(char *)yyjson_get_str(key),
                                                                (char *)yyjson_get_str(value),
                                                                NULL,
                                                                0};
        }
        qsort(default_index, default_count, sizeof(*default_index), compare_dependency);
    }
    for (size_t f = 0; f < sources->count; f++) {
        const module_source_t *file = &sources->files[f];
        source_include_error_t syntax = {0};
        if (!parse_source_imports(file->name, file->source, file->length, &literals, &syntax)) {
            site = (module_import_site_t){f, syntax.row, syntax.column, syntax.offset};
            status = fail(error, MODULE_IMPORTS_SYNTAX, site, syntax.message);
            goto done;
        }
        for (size_t i = 0; i < literals.count; i++) {
            source_include_t *literal = &literals.items[i];
            site = (module_import_site_t){f, literal->row, literal->column, literal->offset};
            if (occurrences == max_imports) {
                status = fail(error, MODULE_IMPORTS_LIMIT, site, "too many import occurrences");
                goto done;
            }
            goat_module_import_t spec;
            if (!goat_module_parse_import(literal->path, strlen(literal->path), &spec)) {
                status =
                    fail(error, MODULE_IMPORTS_IDENTITY, site, "invalid import name or version");
                goto done;
            }
            if (!spec.has_version) {
                const char *version = default_version(default_index, default_count, spec.name);
                if (!version) {
                    status =
                        fail(error, MODULE_IMPORTS_VERSION, site, "import has no default version");
                    goto done;
                }
                spec.version = (goat_module_slice_t){version, strlen(version)};
            }
            if (occurrences == capacity) {
                size_t next = capacity ? capacity * 2 : 16;
                if (next < capacity || next > SIZE_MAX / sizeof(*records)) {
                    status = fail(error, MODULE_IMPORTS_LIMIT, site, "too many import occurrences");
                    goto done;
                }
                import_occurrence_t *fresh = ALLOC(next * sizeof(*fresh));
                if (occurrences)
                    memcpy(fresh, records, occurrences * sizeof(*fresh));
                FREE(records);
                records = fresh;
                capacity = next;
            }
            records[occurrences++] =
                (import_occurrence_t){copy_slice(spec.name.data, spec.name.length),
                                      copy_slice(spec.version.data, spec.version.length),
                                      site};
        }
        clear_source_includes(&literals);
    }
    /* Sort once, then allocate exactly one dependency and one site array per
     * pair. Repeated or distinct imports both avoid quadratic copying/search. */
    if (occurrences > 1)
        qsort(records, occurrences, sizeof(*records), compare_occurrence);
    size_t pairs = 0;
    for (size_t i = 0; i < occurrences; i++)
        if (!i || !same_pair(&records[i - 1], &records[i]))
            pairs++;
    if (pairs > SIZE_MAX / sizeof(*result->dependencies)) {
        status = fail(error, MODULE_IMPORTS_LIMIT, site, "too many dependencies");
        goto done;
    }
    if (pairs)
        result->dependencies = ALLOC(pairs * sizeof(*result->dependencies));
    for (size_t begin = 0; begin < occurrences;) {
        size_t end = begin + 1;
        while (end < occurrences && same_pair(&records[begin], &records[end]))
            end++;
        size_t count = end - begin;
        if (count > SIZE_MAX / sizeof(module_import_site_t)) {
            status = fail(error, MODULE_IMPORTS_LIMIT, site, "too many import sites");
            goto done;
        }
        module_import_dependency_t *dep = &result->dependencies[result->count++];
        *dep = (module_import_dependency_t){
            copy_slice(records[begin].name, strlen(records[begin].name)),
            copy_slice(records[begin].version, strlen(records[begin].version)),
            ALLOC(count * sizeof(*dep->sites)),
            count};
        for (size_t i = begin; i < end; i++)
            dep->sites[i - begin] = records[i].site;
        begin = end;
    }
    if (default_count)
        result->unused_defaults = ALLOC(default_count * sizeof(*result->unused_defaults));
    for (size_t i = 0; i < default_count; i++) {
        const module_import_dependency_t *entry = &default_index[i];
        bool used = result->count
                    && bsearch(entry,
                               result->dependencies,
                               result->count,
                               sizeof(*result->dependencies),
                               compare_dependency);
        if (!used)
            result->unused_defaults[result->unused_count++] =
                (module_import_dependency_t){copy_slice(entry->name, strlen(entry->name)),
                                             copy_slice(entry->version, strlen(entry->version)),
                                             NULL,
                                             0};
    }
    *out = result;
    result = NULL;
done:
    for (size_t i = 0; i < occurrences; i++) {
        FREE(records[i].name);
        FREE(records[i].version);
    }
    FREE(records);
    FREE(default_index);
    clear_source_includes(&literals);
    yyjson_doc_free(descriptor);
    free_module_imports(result);
    return status;
}
