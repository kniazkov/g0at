/** @file module_files.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Separate persistent file scopes over a single validated bytecode image.
 */
#include "module_files.h"

#include "lib/allocate.h"
#include "module/module_json_internal.h"
#include "module_file_declarations.h"
#include "process.h"

#include <assert.h>

struct module_file_t {
    module_files_t *owner;
    uint32_t id;
    string_value_t name;
    bool external;
    object_t *scope;
    module_file_state_t state;
    bool direct_entry;
    exception_t failure;
    module_file_t *initialization_parent;
};

/** @brief Diagnostic stack follows execution, not an unrelated OS thread. */
static _Thread_local module_file_t *current_initializing_file;

struct module_files_t {
    module_files_t *next;
    process_t *process;
    module_t *identity;
    bytecode_t *code;
    uint32_t main;
    string_value_t name, version;
    module_file_t *files;
    object_t **strings;
    module_file_t **imports;
};

/** Take ownership of the wrapper, borrowing parsed metadata only during construction. */
static module_files_t *create_files(process_t *process, const yyjson_doc *doc, bytecode_t *code) {
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
    yyjson_val *name = yyjson_obj_get(root, "name");
    yyjson_val *version = yyjson_obj_get(root, "version");
    set->name = decode_utf8_n(yyjson_get_str(name), yyjson_get_len(name));
    set->version = decode_utf8_n(yyjson_get_str(version), yyjson_get_len(version));
    if (code->data_descriptor_count) {
        set->strings = CALLOC(code->data_descriptor_count * sizeof(*set->strings));
        set->imports = CALLOC(code->data_descriptor_count * sizeof(*set->imports));
    }
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
        file->scope = create_user_defined_object_in_file(process,
                                                         (object_array_t){&builtins, 1},
                                                         set->identity,
                                                         file->id);
        set_inheritance_prototype_count(file->scope, 0);
    }
    set->next = process->module_files;
    process->module_files = set;
    return set;
}

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
    if (code->file_count > SIZE_MAX / sizeof(module_file_t)
        || code->data_descriptor_count > SIZE_MAX / sizeof(void *)) {
        yyjson_doc_free(doc);
        free_bytecode(code);
        if (error)
            *error =
                (module_bytecode_error_t){MODULE_BYTECODE_IMAGE, SIZE_MAX, GOAT_MODULE_JSON_OK};
        return MODULE_BYTECODE_IMAGE;
    }
    *out = create_files(process, doc, code);
    yyjson_doc_free(doc);
    return MODULE_BYTECODE_OK;
}

bool create_module_files_from_snapshot(process_t *process,
                                       const yyjson_doc *metadata,
                                       const bytecode_t *code,
                                       module_files_t **out) {
    if (out)
        *out = NULL;
    if (!process || !metadata || !code || !out
        || (code->file_count > SIZE_MAX / sizeof(module_file_t)
            || code->data_descriptor_count > SIZE_MAX / sizeof(void *)))
        return false;
    *out = create_files(process, metadata, borrow_bytecode_image(code));
    return true;
}

size_t module_files_count(const module_files_t *files) {
    return files ? files->code->file_count : 0;
}

module_file_t *module_files_get(module_files_t *files, uint32_t id) {
    return files && id < files->code->file_count ? files->files + id : NULL;
}

module_files_t *find_module_files(process_t *process, const module_t *module) {
    if (process && module)
        for (module_files_t *set = process->module_files; set; set = set->next)
            if (set->identity == module)
                return set;
    return NULL;
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

bool bind_module_files_import(module_files_t *consumer,
                              uint32_t descriptor,
                              module_files_t *target) {
    if (!consumer || !target || consumer->process != target->process
        || descriptor >= consumer->code->data_descriptor_count)
        return false;
    module_file_t *main = module_files_main(target);
    if (consumer->imports[descriptor] && consumer->imports[descriptor] != main)
        return false;
    consumer->imports[descriptor] = main;
    return true;
}

module_file_t *module_files_import(module_files_t *consumer, uint32_t descriptor) {
    return consumer && descriptor < consumer->code->data_descriptor_count
               ? consumer->imports[descriptor]
               : NULL;
}

object_t *module_files_string(module_files_t *files, uint32_t descriptor) {
    if (!files || descriptor >= files->code->data_descriptor_count)
        return NULL;
    object_t *string = files->strings[descriptor];
    if (!string) {
        const data_descriptor_t *data = files->code->data_descriptors + descriptor;
        string = create_string_object(
            files->process,
            (string_value_t){(const gchar_t *)(files->code->data + data->offset),
                             data->size / sizeof(gchar_t) - 1,
                             false});
        files->strings[descriptor] = string;
    }
    return string;
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
    init_function_access_in_file(&context->access, file->owner->identity, file->id);
    add_function_access_domain(&context->access, get_object_access_domain(file->scope));
    return context;
}

void mark_module_files(const process_t *process) {
    for (module_files_t *set = process->module_files; set; set = set->next) {
        for (size_t i = 0; i < set->code->data_descriptor_count; ++i)
            if (set->strings[i])
                mark_object(set->strings[i]);
        for (size_t i = 0; i < set->code->file_count; ++i) {
            mark_object(set->files[i].scope);
            if (set->files[i].failure.value)
                mark_object(set->files[i].failure.value);
        }
    }
}

void destroy_module_files(process_t *process) {
    /* Process teardown releases all managed objects without reference traversal.
       Keep image-backed strings alive until that sweep has finished. */
    while (process->module_files) {
        module_files_t *set = process->module_files;
        process->module_files = set->next;
        for (size_t i = 0; i < set->code->file_count; ++i) {
            FREE_STRING(set->files[i].name);
            release_exception_trace(set->files[i].failure.trace);
        }
        FREE_STRING(set->name);
        FREE_STRING(set->version);
        FREE(set->strings);
        FREE(set->imports);
        FREE(set->files);
        free_bytecode(set->code);
        release_module_identity(set->identity);
        FREE(set);
    }
}

module_file_state_t module_file_state(const module_file_t *file) {
    if (!file)
        return MODULE_FILE_UNINITIALIZED;
    process_t *process = file->owner->process;
    process_execution_enter(process);
    module_file_state_t state = file->state;
    process_execution_leave(process);
    return state;
}

bytecode_t *module_file_bytecode(const module_file_t *file) {
    return file ? file->owner->code : NULL;
}

process_t *module_file_process(const module_file_t *file) {
    return file ? file->owner->process : NULL;
}

/** @brief Wait only for a foreign included initializer, never for ordinary direct main. */
static void await_file_publication(const module_file_t *file) {
    process_t *process = file->owner->process;
    if (file->state != MODULE_FILE_INITIALIZING || file->direct_entry
        || os_mutex_depth(process->initialization_mutex))
        return;
    ++process->initialization_waiters;
    broadcast_os_condition(process->threads_changed);
    size_t depth = process_execution_suspend(process);
    os_thread_status_t status = lock_os_mutex(process->initialization_mutex);
    assert(status == OS_THREAD_OK);
    process_execution_resume(process, depth);
    --process->initialization_waiters;
    status = unlock_os_mutex(process->initialization_mutex);
    assert(status == OS_THREAD_OK);
    (void)status;
}

object_t *module_file_failure(const module_file_t *file) {
    return module_file_exception(file).value;
}

exception_t module_file_exception(const module_file_t *file) {
    if (!file)
        return (exception_t){0};
    process_t *process = file->owner->process;
    process_execution_enter(process);
    await_file_publication(file);
    exception_t result = file->state == MODULE_FILE_FAILED ? file->failure : (exception_t){0};
    process_execution_leave(process);
    return result;
}

exception_t module_files_exception(process_t *process, module_t *module, uint32_t file_id) {
    if (process && module)
        for (module_files_t *set = process->module_files; set; set = set->next)
            if (set->identity == module)
                return module_file_exception(module_files_get(set, file_id));
    return (exception_t){0};
}

object_t *module_files_failure(process_t *process, module_t *module, uint32_t file_id) {
    if (!process || !module)
        return NULL;
    for (module_files_t *set = process->module_files; set; set = set->next)
        if (set->identity == module)
            return module_file_failure(module_files_get(set, file_id));
    return NULL;
}

module_file_t *module_scope_file(const object_t *scope) {
    if (!scope || !scope->process)
        return NULL;
    for (module_files_t *set = scope->process->module_files; set; set = set->next)
        for (size_t i = 0; i < set->code->file_count; ++i)
            if (set->files[i].scope == scope)
                return set->files + i;
    return NULL;
}

object_t *module_scope_failure(const object_t *scope) {
    return module_file_failure(module_scope_file(scope));
}

static module_initialize_status_t initialize_file(module_file_t *file,
                                                  context_t *caller,
                                                  module_file_executor_t execute,
                                                  void *data,
                                                  object_t **scope,
                                                  exception_t *failure,
                                                  bool direct) {
    if (scope)
        *scope = NULL;
    if (failure)
        *failure = (exception_t){0};
    if (!file || !caller || !execute || !scope || !failure
        || (caller != get_root_context() && caller->data->process != file->owner->process))
        return MODULE_INITIALIZE_ARGUMENT;
    process_t *process = file->owner->process;
    process_execution_enter(process);
    bool locked = false;
    /* Completed files do not contend on the initializer lock. A directly executing
       entry file exposes declaration readiness, not all-or-nothing file readiness. */
    if (file->state == MODULE_FILE_UNINITIALIZED
        || (file->state == MODULE_FILE_INITIALIZING && !file->direct_entry)) {
        if (!direct) {
            ++process->initialization_waiters;
            broadcast_os_condition(process->threads_changed);
            size_t depth = process_execution_suspend(process);
            os_thread_status_t status = lock_os_mutex(process->initialization_mutex);
            assert(status == OS_THREAD_OK);
            (void)status;
            process_execution_resume(process, depth);
            --process->initialization_waiters;
            locked = true;
        }
    }
    module_initialize_status_t result = MODULE_INITIALIZE_OK;
    if (file->state == MODULE_FILE_FAILED) {
        *failure = retain_exception(file->failure);
        result = MODULE_INITIALIZE_FAILED;
        goto done;
    }
    if (file->state == MODULE_FILE_INITIALIZED || file->state == MODULE_FILE_INITIALIZING) {
        *scope = file->scope;
        goto done;
    }
    file->direct_entry = direct;
    file->state = MODULE_FILE_INITIALIZING;
    file->initialization_parent = current_initializing_file;
    current_initializing_file = file;
    module_file_t *previous = process->initializing_file;
    if (!direct)
        process->initializing_file = file;
    bool success = prepare_module_file_declarations(file);
    if (success) {
        context_t *frame = create_module_file_context(file, caller);
        frame->file_initialization_boundary = true;
        success = execute(file, frame, data, failure);
        if (failure->value && !failure->trace)
            failure->trace = capture_exception_trace(frame, frame->instruction);
        destroy_context(frame);
    }
    if (!direct)
        process->initializing_file = previous;
    current_initializing_file = file->initialization_parent;
    file->initialization_parent = NULL;
    success = success && !failure->value;
    if (!success) {
        if (!failure->value) {
            failure->value = get_exception_invalid_operation();
            INCREF(failure->value);
        }
        file->failure = retain_exception(*failure);
        result = MODULE_INITIALIZE_FAILED;
    } else {
        *scope = file->scope;
    }
    /* Value/trace/readiness become observable together under execution_mutex. */
    file->state = success ? MODULE_FILE_INITIALIZED : MODULE_FILE_FAILED;
done:
    if (locked) {
        os_thread_status_t status = unlock_os_mutex(process->initialization_mutex);
        assert(status == OS_THREAD_OK);
        (void)status;
    }
    process_execution_leave(process);
    return result;
}

module_initialize_status_t initialize_module_file(module_file_t *file,
                                                  context_t *caller,
                                                  module_file_executor_t execute,
                                                  void *data,
                                                  object_t **scope,
                                                  exception_t *failure) {
    return initialize_file(file, caller, execute, data, scope, failure, false);
}

module_initialize_status_t initialize_module_file_direct(module_file_t *file,
                                                         context_t *caller,
                                                         module_file_executor_t execute,
                                                         void *data,
                                                         object_t **scope,
                                                         exception_t *failure) {
    return initialize_file(file, caller, execute, data, scope, failure, true);
}

bool module_initialization_active(process_t *process) {
    return current_initializing_file && current_initializing_file->owner->process == process;
}

static void append_initialization_file(string_builder_t *builder, const module_file_t *file) {
    string_value_t escaped = string_to_string_notation(U"", file->name);
    append_string_value(builder, escaped);
    FREE_STRING(escaped);
    string_value_t suffix =
        format_string(file->external ? U" [external file %u]" : U" [file %u]", file->id);
    append_string_value(builder, suffix);
    FREE_STRING(suffix);
}

string_value_t module_initialization_chain(process_t *process, const module_file_t *target) {
    if (!process || (target && target->owner->process != process))
        return EMPTY_STRING_VALUE;
    size_t count = 0;
    for (module_file_t *file = current_initializing_file; file; file = file->initialization_parent)
        ++count;
    module_file_t **chain = count ? ALLOC(count * sizeof(*chain)) : NULL;
    size_t index = count;
    for (module_file_t *file = current_initializing_file; file; file = file->initialization_parent)
        chain[--index] = file;
    string_builder_t builder;
    init_string_builder(&builder, 0);
    for (size_t i = 0; i < count; ++i) {
        if (i)
            append_static_string(&builder, U" -> ");
        append_initialization_file(&builder, chain[i]);
    }
    FREE(chain);
    if (target && current_initializing_file != target) {
        if (count)
            append_static_string(&builder, U" -> ");
        append_initialization_file(&builder, target);
    }
    return (string_value_t){builder.data, builder.length, builder.data != NULL};
}

string_view_t module_files_name(const module_files_t *files) {
    return files ? (string_view_t){files->name.data, files->name.length} : (string_view_t){0};
}

string_view_t module_files_version(const module_files_t *files) {
    return files ? (string_view_t){files->version.data, files->version.length} : (string_view_t){0};
}
