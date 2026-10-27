/** @file module_writer.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Standard ZIP profile and validated same-directory publication.
 */
#include "module_writer.h"

#include "lib/memory/allocate.h"

#include <stdlib.h>
#include <string.h>

void clear_module_writer_error(module_writer_error_t *error) {
    if (error) {
        clear_fs_error(&error->filesystem);
        clear_fs_error(&error->cleanup);
        *error = (module_writer_error_t){0};
    }
}

static module_writer_status_t fail(module_writer_error_t *error, module_writer_status_t status) {
    if (error)
        error->status = status;
    return status;
}

static zip_write_entry_t entry(const char *name, const void *data, size_t size) {
    return (zip_write_entry_t){.name = {(const uint8_t *)name, strlen(name)},
                               .data = {data, size},
                               .method = 8,
                               .flags = 0x0800,
                               .time = 0,
                               .date = 33,
                               .made_by = 0x0314,
                               .external_attributes = UINT32_C(0100644) << 16};
}

static int entry_compare(const void *left, const void *right) {
    const zip_write_entry_t *a = left, *b = right;
    size_t n = a->name.size < b->name.size ? a->name.size : b->name.size;
    int cmp = memcmp(a->name.data, b->name.data, n);
    return cmp ? cmp : (a->name.size > b->name.size) - (a->name.size < b->name.size);
}

static bool bytes_valid(zip_view_t view) {
    return view.data || !view.size;
}

module_writer_status_t module_write_memory(const module_writer_input_t *input,
                                           const module_writer_options_t *options,
                                           void **out,
                                           size_t *size,
                                           module_writer_error_t *error) {
    if (out)
        *out = NULL;
    if (size)
        *size = 0;
    clear_module_writer_error(error);
    if (!out || !size || !input || !input->metadata || !bytes_valid(input->bytecode)
        || (input->native_count && !input->native))
        return fail(error, MODULE_WRITER_ARGUMENT);
    /* ZIP32 count and length sentinels are forbidden even on 64-bit processes. */
    if (input->native_count >= UINT16_MAX - 2 || input->bytecode.size >= UINT32_MAX)
        return fail(error, MODULE_WRITER_LIMIT);
    size_t maximum = options ? options->max_archive : SIZE_MAX;
    size_t expanded_limit = options ? options->max_expanded : SIZE_MAX;
    size_t expanded = input->bytecode.size;
    for (size_t i = 0; i < input->native_count; i++) {
        const module_writer_native_t *part = input->native + i;
        if (!part->name || !part->name_length || memchr(part->name, 0, part->name_length)
            || !bytes_valid(part->bytes))
            return fail(error, MODULE_WRITER_ARGUMENT);
        if (part->bytes.size >= UINT32_MAX || part->bytes.size > SIZE_MAX - expanded)
            return fail(error, MODULE_WRITER_LIMIT);
        expanded += part->bytes.size;
        for (size_t j = 0; j < i; j++)
            if (part->name_length == input->native[j].name_length
                && !memcmp(part->name, input->native[j].name, part->name_length))
                return fail(error, MODULE_WRITER_PARTS);
    }
    if (expanded > expanded_limit)
        return fail(error, MODULE_WRITER_LIMIT);
    char *canonical = NULL;
    size_t length = 0;
    goat_module_json_error json =
        goat_module_json_write(input->metadata, GOAT_MODULE_METADATA, false, &canonical, &length);
    if (json) {
        if (error)
            error->json = json;
        return fail(error, MODULE_WRITER_JSON);
    }
    yyjson_doc *normalized = NULL;
    json = goat_module_json_read(canonical, length, GOAT_MODULE_METADATA, &normalized);
    if (json) {
        if (error)
            error->json = json;
        goat_module_json_free(canonical);
        return fail(error, MODULE_WRITER_JSON);
    }
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(normalized, NULL);
    yyjson_doc_free(normalized);
    if (!doc) {
        goat_module_json_free(canonical);
        return fail(error, MODULE_WRITER_MEMORY);
    }
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    yyjson_mut_val *variants = yyjson_mut_obj_get(root, "native");
    size_t count = yyjson_mut_arr_size(variants);
    module_writer_status_t status = MODULE_WRITER_OK;
    zip_write_entry_t *entries = NULL;
    void *archive = NULL;
    size_t archive_size = 0;
    yyjson_doc *final_metadata = NULL;
    validated_module_t *validated = NULL;
    if (count != input->native_count) {
        status = MODULE_WRITER_PARTS;
        goto done;
    }
    entries = calloc(count + 2, sizeof(*entries));
    if (!entries) {
        status = MODULE_WRITER_MEMORY;
        goto done;
    }
    for (size_t i = 0; i < count; i++) {
        yyjson_mut_val *variant = yyjson_mut_arr_get(variants, i);
        yyjson_mut_val *library = yyjson_mut_obj_get(variant, "library");
        const module_writer_native_t *part = NULL;
        for (size_t j = 0; j < count; j++)
            if (input->native[j].name_length == yyjson_mut_get_len(library)
                && !memcmp(input->native[j].name,
                           yyjson_mut_get_str(library),
                           input->native[j].name_length)) {
                part = input->native + j;
                break;
            }
        if (!part) {
            status = MODULE_WRITER_PARTS;
            goto done;
        }
        entries[i + 2] = entry(yyjson_mut_get_str(library), part->bytes.data, part->bytes.size);
    }
    /* Validate the original parts before rewriting names. Otherwise a forbidden
     * path or a target/suffix mismatch could be laundered by renaming. */
    entries[0] = entry("module.json", canonical, length);
    entries[1] = entry("bytecode.bin", input->bytecode.data, input->bytecode.size);
    zip_status_t original_zip =
        zip_archive_write(entries, count + 2, (zip_view_t){0}, SIZE_MAX, &archive, &archive_size);
    if (original_zip) {
        if (error)
            error->zip = original_zip;
        status = MODULE_WRITER_ZIP;
        goto done;
    }
    module_validation_options_t original_options = {.max_archive = SIZE_MAX,
                                                    .max_expanded = expanded_limit};
    module_validation_result_t original_result =
        validate_module(archive, archive_size, "writer input", &original_options, &validated);
    if (error)
        error->validation = original_result;
    if (original_result.status) {
        status = MODULE_WRITER_VALIDATION;
        goto done;
    }
    free_validated_module(validated);
    validated = NULL;
    zip_archive_free(archive);
    archive = NULL;
    goat_module_json_free(canonical);
    canonical = NULL;
    for (size_t i = 0; i < count; i++) {
        yyjson_mut_val *variant = yyjson_mut_arr_get(variants, i);
        yyjson_mut_val *target = yyjson_mut_obj_get(variant, "target");
        const char *os = yyjson_mut_get_str(yyjson_mut_obj_get(target, "os"));
        char name[64];
        snprintf(name,
                 sizeof(name),
                 "native/variant-%zu.%s",
                 i + 1,
                 !strcmp(os, "windows") ? "dll" : "so");
        yyjson_mut_val *renamed = yyjson_mut_strcpy(doc, name);
        if (!renamed || !yyjson_mut_obj_put(variant, yyjson_mut_str(doc, "library"), renamed)) {
            status = MODULE_WRITER_MEMORY;
            goto done;
        }
        entries[i + 2].name =
            (zip_view_t){(const uint8_t *)yyjson_mut_get_str(renamed), strlen(name)};
    }
    final_metadata = yyjson_mut_doc_imut_copy(doc, NULL);
    if (!final_metadata) {
        status = MODULE_WRITER_MEMORY;
        goto done;
    }
    /* Renaming may alter the tie-break ordering; apply the common normalization
     * again. Bytecode/function indices and informational author order stay intact. */
    json = goat_module_json_write(final_metadata, GOAT_MODULE_METADATA, false, &canonical, &length);
    if (json) {
        status = MODULE_WRITER_JSON;
        goto done;
    }
    if (length >= UINT32_MAX || length > SIZE_MAX - expanded
        || expanded + length > expanded_limit) {
        status = MODULE_WRITER_LIMIT;
        goto done;
    }
    entries[0] = entry("module.json", canonical, length);
    entries[1] = entry("bytecode.bin", input->bytecode.data, input->bytecode.size);
    qsort(entries + 2, count, sizeof(*entries), entry_compare);
    zip_status_t zip =
        zip_archive_write(entries, count + 2, (zip_view_t){0}, maximum, &archive, &archive_size);
    if (zip) {
        if (error)
            error->zip = zip;
        status = zip == ZIP_SIZE || zip == ZIP_CAPACITY ? MODULE_WRITER_LIMIT : MODULE_WRITER_ZIP;
        goto done;
    }
    module_validation_options_t validation_options = {.max_archive = maximum,
                                                      .max_expanded = expanded_limit};
    module_validation_result_t result =
        validate_module(archive, archive_size, "writer result", &validation_options, &validated);
    if (error)
        error->validation = result;
    if (result.status) {
        status = MODULE_WRITER_VALIDATION;
        goto done;
    }
    *out = archive;
    *size = archive_size;
    archive = NULL;
done:
    if (error)
        error->json = json;
    free_validated_module(validated);
    zip_archive_free(archive);
    goat_module_json_free(canonical);
    yyjson_doc_free(final_metadata);
    yyjson_mut_doc_free(doc);
    free(entries);
    return fail(error, status);
}

void free_module_writer_bytes(void *bytes) {
    zip_archive_free(bytes);
}

static bool
destination_error(fs_error_t *error, fs_status_t status, const char *operation, const char *path) {
    clear_fs_error(error);
    error->status = status;
    error->operation = operation;
    error->path = ALLOC(strlen(path) + 1);
    strcpy(error->path, path);
    return false;
}

static bool destination(const char *path, fs_info_t *info, bool *exists, fs_error_t *error) {
    *exists = false;
    if (inspect_fs_path(path, false, info, error)) {
        *exists = true;
        return info->kind == FS_FILE
               || destination_error(error, FS_INVALID, "destination must be a regular file", path);
    }
    if (error->status != FS_NOT_FOUND)
        return false;
    clear_fs_error(error);
    return true;
}

static bool
same_destination(const char *path, const fs_info_t *original, bool existed, fs_error_t *error) {
    fs_info_t current;
    bool exists;
    if (!destination(path, &current, &exists, error))
        return false;
    return (exists == existed
            && (!exists
                || (current.device == original->device && current.identity == original->identity)))
           || destination_error(error, FS_CHANGED, "destination changed before publication", path);
}

module_writer_status_t module_write_file(const char *path,
                                         const module_writer_input_t *input,
                                         const module_writer_options_t *options,
                                         module_writer_error_t *error) {
    module_writer_error_t local = {0};
    if (!error)
        error = &local;
    clear_module_writer_error(error);
    fs_info_t original;
    bool existed;
    module_writer_status_t status;
    void *bytes = NULL;
    size_t size = 0;
    char *temporary = NULL;
    FILE *file = NULL;
    validated_module_t *verified = NULL;
    if (!path) {
        status = MODULE_WRITER_ARGUMENT;
        goto done;
    }
    if (!destination(path, &original, &existed, &error->filesystem)) {
        status = MODULE_WRITER_DESTINATION;
        goto done;
    }
    status = module_write_memory(input, options, &bytes, &size, error);
    if (status)
        goto done;
    temporary = create_fs_temporary(path, false, &file, &error->filesystem);
    if (!temporary) {
        status = MODULE_WRITER_IO;
        goto done;
    }
    bool written = write_fs_file(file, bytes, size, temporary, &error->filesystem);
    fs_error_t close_error = {0};
    bool closed = close_fs_file(file, temporary, &close_error);
    if (written && !closed) {
        clear_fs_error(&error->filesystem);
        error->filesystem = close_error;
        close_error = (fs_error_t){0};
    }
    clear_fs_error(&close_error);
    if (!written || !closed) {
        status = MODULE_WRITER_IO;
        goto done;
    }
    module_validation_options_t validation_options = {
        .max_archive = options ? options->max_archive : SIZE_MAX,
        .max_expanded = options ? options->max_expanded : SIZE_MAX};
    error->validation = validate_module_file(temporary, &validation_options, &verified);
    if (error->validation.status) {
        status = MODULE_WRITER_VALIDATION;
        goto done;
    }
    if (!same_destination(path, &original, existed, &error->filesystem)) {
        status = MODULE_WRITER_DESTINATION;
        goto done;
    }
    if (!prepare_fs_replace(temporary, path, &error->filesystem)) {
        status = MODULE_WRITER_IO;
        goto done;
    }
    if (!same_destination(path, &original, existed, &error->filesystem)) {
        status = MODULE_WRITER_DESTINATION;
        goto done;
    }
    if (!publish_fs_replace(temporary, path, &error->filesystem)) {
        status = MODULE_WRITER_IO;
        goto done;
    }
done:
    if (temporary && status)
        remove_fs_entry(temporary, &error->cleanup);
    free_validated_module(verified);
    FREE(temporary);
    free_module_writer_bytes(bytes);
    fail(error, status);
    clear_module_writer_error(&local);
    return status;
}
