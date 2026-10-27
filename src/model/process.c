/**
 * @file process.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implements functions for managing processes in the Goat programming language.
 */

#include "process.h"

#include "access.h"
#include "context.h"
#include "lib/memory/allocate.h"
#include "lib/process/process_exit.h"
#include "module_files.h"
#include "object_list.h"
#include "thread.h"

#include <assert.h>
#include <stdatomic.h>
#include <string.h>

/** @brief ID of the last created process. */
static atomic_uint_fast64_t last_process_id;

process_t *create_process() {
    /* Prearm before any user/native code can hold a Windows loader lock.
       If unavailable, valid exit still terminates immediately with its code. */
    (void)process_exit_prepare();
    process_t *process = (process_t *)CALLOC(sizeof(process_t));
    if (create_os_recursive_mutex(&process->execution_mutex) != OS_THREAD_OK
        || create_os_recursive_mutex(&process->initialization_mutex) != OS_THREAD_OK
        || create_os_condition(&process->threads_changed) != OS_THREAD_OK) {
        destroy_os_condition(process->threads_changed);
        destroy_os_mutex(process->initialization_mutex);
        destroy_os_mutex(process->execution_mutex);
        FREE(process);
        return NULL;
    }
    process->id = atomic_fetch_add(&last_process_id, 1) + 1;
    process->module = create_module_identity();
    init_object_list(&process->objects);
    init_object_list(&process->integers);
    init_object_list(&process->real_numbers);
    init_object_list(&process->dynamic_strings);
    init_object_list(&process->user_defined_objects);
    init_object_list(&process->characters);
    create_thread(process, create_context(process, get_root_context(), NULL));
    return process;
}

/** @brief Destroys all objects in the given list by calling their release function. */
static void destroy_all_objects_in_the_list(object_list_t *list) {
    object_t *object = list->head;
    while (object) {
        object_t *next = object->next;
        release_object(object);
        object = next;
    }
}

void destroy_process(process_t *process) {
    while (process->main_thread) {
        destroy_thread(process->main_thread);
    }
    DECREFIF(process->launch_directory);
    release_module_identity(process->launch_module);
    destroy_all_objects_in_the_list(&process->objects);
    destroy_all_objects_in_the_list(&process->integers);
    destroy_all_objects_in_the_list(&process->real_numbers);
    destroy_all_objects_in_the_list(&process->dynamic_strings);
    destroy_all_objects_in_the_list(&process->user_defined_objects);
    destroy_all_objects_in_the_list(&process->characters);
    destroy_module_files(process);
    FREE(process->string_cache);
    FREE(process->gc_roots);
    release_module_identity(process->module);
    destroy_os_condition(process->threads_changed);
    destroy_os_mutex(process->initialization_mutex);
    destroy_os_mutex(process->execution_mutex);
    FREE(process);
}

bool prepare_process_launch(process_t *process,
                            module_t *module,
                            uint32_t file_id,
                            const char *canonical_path) {
    if (!process || !module || !canonical_path || process->launch_module)
        return false;
    const char *slash = strrchr(canonical_path, '/');
    if (!slash)
        return false;
    size_t length = (size_t)(slash - canonical_path);
    if (!length)
        length = 1;
#ifdef _WIN32
    if (length == 2 && canonical_path[1] == ':')
        length = 3;
#endif
    string_value_t directory = decode_utf8_n(canonical_path, length);
    if (!directory.data)
        return false;
    process->launch_directory = create_string_object(process, directory);
    process->launch_module = retain_module_identity(module);
    process->launch_file = file_id;
    return true;
}

void process_execution_enter(process_t *process) {
    os_thread_status_t status = lock_os_mutex(process->execution_mutex);
    assert(status == OS_THREAD_OK);
    (void)status;
}

void process_execution_leave(process_t *process) {
    os_thread_status_t status = unlock_os_mutex(process->execution_mutex);
    assert(status == OS_THREAD_OK);
    (void)status;
}

size_t process_execution_suspend(process_t *process) {
    size_t depth = os_mutex_depth(process->execution_mutex);
    for (size_t i = 0; i < depth; ++i)
        process_execution_leave(process);
    return depth;
}

void process_execution_resume(process_t *process, size_t depth) {
    for (size_t i = 0; i < depth; ++i)
        process_execution_enter(process);
}

operation_result_t request_program_exit(thread_t *thread, int64_t code) {
    if (!thread || code < 0 || code > 255)
        return operation_exception(get_exception_invalid_argument());
    if (thread->initialization_depth)
        return operation_exception(get_exception_exit_during_initialization());
    process_exit_request((unsigned)code);
}
