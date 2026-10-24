/** @file module_files.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Separate persistent file scopes over a single validated bytecode image.
 */
#include "module_files.h"

#include "lib/allocate.h"
#include "module/module_json_internal.h"
#include "process.h"

#include <assert.h>

struct module_file_t {
    module_files_t *owner;
    uint32_t id;
    string_value_t name;
    bool external;
    object_t *scope;
};

struct module_files_t {
    module_files_t *next;
    process_t *process;
    module_t *identity;
    bytecode_t *code;
    uint32_t main;
    module_file_t *files;
};

module_bytecode_status_t create_module_files(process_t *process,
                                             const char *metadata,
                                             size_t metadata_size,
                                             const void *image,
                                             size_t image_size,
                                             module_files_t **out,
                                             module_bytecode_error_t *error) {
    if (out)
        *out = NULL;
    if (!process || !out) {
        if (error)
            *error =
                (module_bytecode_error_t){MODULE_BYTECODE_ARGUMENT, SIZE_MAX, GOAT_MODULE_JSON_OK};
        return MODULE_BYTECODE_ARGUMENT;
    }
    bytecode_t *code = NULL;
    uint32_t entry;
    module_bytecode_status_t status =
        read_module_bytecode(metadata, metadata_size, image, image_size, &code, &entry, error);
    if (status)
        return status;
    yyjson_doc *doc = NULL;
    goat_module_json_error json =
        goat_module_json_read(metadata, metadata_size, GOAT_MODULE_METADATA, &doc);
    if (json) {
        free_bytecode(code);
        if (error)
            *error = (module_bytecode_error_t){MODULE_BYTECODE_JSON, SIZE_MAX, json};
        return MODULE_BYTECODE_JSON;
    }
    if (code->file_count > SIZE_MAX / sizeof(module_file_t)) {
        yyjson_doc_free(doc);
        free_bytecode(code);
        if (error)
            *error =
                (module_bytecode_error_t){MODULE_BYTECODE_IMAGE, SIZE_MAX, GOAT_MODULE_JSON_OK};
        return MODULE_BYTECODE_IMAGE;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    int64_t main = 0;
    bool valid = goat_json_integer(yyjson_obj_get(root, "main"), &main);
    assert(valid && main >= 0 && (uint64_t)main < code->file_count);
    (void)valid;
    module_files_t *set = CALLOC(sizeof(*set));
    set->process = process;
    set->identity = create_module_identity();
    set->code = code;
    set->main = (uint32_t)main;
    set->files = CALLOC(code->file_count * sizeof(*set->files));
    yyjson_val *records = yyjson_obj_get(root, "files");
    object_t *builtins = get_root_context()->data;
    for (size_t i = 0; i < code->file_count; ++i) {
        module_file_t *file = set->files + i;
        file->owner = set;
        file->id = (uint32_t)i;
        yyjson_val *record = yyjson_arr_get(records, i);
        yyjson_val *name = yyjson_obj_get(record, "name");
        file->name = decode_utf8_n(yyjson_get_str(name), yyjson_get_len(name));
        file->external = yyjson_get_bool(yyjson_obj_get(record, "external"));
        file->scope = create_user_defined_object_in_module(process,
                                                           (object_array_t){&builtins, 1},
                                                           set->identity);
        set_inheritance_prototype_count(file->scope, 0);
    }
    yyjson_doc_free(doc);
    set->next = process->module_files;
    process->module_files = set;
    *out = set;
    return MODULE_BYTECODE_OK;
}

size_t module_files_count(const module_files_t *files) {
    return files ? files->code->file_count : 0;
}

module_file_t *module_files_get(module_files_t *files, uint32_t id) {
    return files && id < files->code->file_count ? files->files + id : NULL;
}

module_file_t *module_files_main(module_files_t *files) {
    return files ? module_files_get(files, files->main) : NULL;
}

bytecode_t *module_files_bytecode(module_files_t *files) {
    return files ? files->code : NULL;
}

module_t *module_files_identity(module_files_t *files) {
    return files ? files->identity : NULL;
}

uint32_t module_file_id(const module_file_t *file) {
    return file->id;
}

instr_index_t module_file_entry(const module_file_t *file) {
    return file->owner->code->files[file->id].entry;
}

object_t *module_file_scope(const module_file_t *file) {
    return file->scope;
}

string_view_t module_file_name(const module_file_t *file) {
    return (string_view_t){file->name.data, file->name.length};
}

bool module_file_external(const module_file_t *file) {
    return file->external;
}

context_t *create_module_file_context(module_file_t *file, context_t *caller) {
    if (!file || !caller
        || (caller != get_root_context() && caller->data->process != file->owner->process))
        return NULL;
    object_t *builtins = get_root_context()->data;
    context_t *context = create_context_with_protos_in_module(file->owner->process,
                                                              caller,
                                                              (object_array_t){&builtins, 1},
                                                              file->owner->identity);
    /* Replace the fresh frame's transient declaration domain with the persistent
       file's domain; no observer rights are taken from the dynamic caller. */
    clear_function_access(&context->access, true);
    DECREF(context->data);
    context->data = file->scope;
    INCREF(context->data);
    init_function_access(&context->access, file->owner->identity);
    add_function_access_domain(&context->access, get_object_access_domain(file->scope));
    return context;
}

void mark_module_files(const process_t *process) {
    for (module_files_t *set = process->module_files; set; set = set->next)
        for (size_t i = 0; i < set->code->file_count; ++i)
            mark_object(set->files[i].scope);
}

void destroy_module_files(process_t *process) {
    /* Process teardown releases all managed objects without reference traversal.
       Keep image-backed strings alive until that sweep has finished. */
    while (process->module_files) {
        module_files_t *set = process->module_files;
        process->module_files = set->next;
        for (size_t i = 0; i < set->code->file_count; ++i)
            FREE_STRING(set->files[i].name);
        FREE(set->files);
        free_bytecode(set->code);
        release_module_identity(set->identity);
        FREE(set);
    }
}
