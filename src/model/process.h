/**
 * @file process.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the structure for processes in the Goat programming language.
 */

#pragma once

#include "lib/os_thread.h"
#include "object_list.h"

#include <stdint.h>

typedef struct process_t process_t;
typedef struct thread_t thread_t;
typedef struct module_t module_t;

/** @brief A process in Goat. */
struct process_t {
    /** @brief Short recursive model lock; never retained across user blocking waits. */
    os_mutex_t *execution_mutex;
    /** @brief One recursive initializer lock for all files in this VM. */
    os_mutex_t *initialization_mutex;
    /** @brief Program-thread completion notification, guarded by execution_mutex. */
    os_condition_t *threads_changed;
    size_t active_threads; /**< Unfinished program executions, including descendants. */
    /** @brief Loader waiters, guarded by execution_mutex, for lifecycle diagnostics. */
    size_t initialization_waiters;
    struct vm_execution_t *execution; /**< Borrowed while the VM owns execution. */

    /** @brief Owned identity of the single program module until module loading is added. */
    module_t *module;
    /** @brief Owned per-module file scopes/images, destroyed after managed objects. */
    struct module_files_t *module_files;
    /** @brief Borrowed top of the serialized included-file initialization stack. */
    struct module_file_t *initializing_file;
    /** @brief Frozen direct-entry identity and shared directory, outside immutable images. */
    module_t *launch_module;
    uint32_t launch_file;       /**< Entry source index inside launch_module. */
    object_t *launch_directory; /**< Owned shared directory, also a GC root. */

    /** @brief Host-requested VM cancellation; language exit terminates the OS process. */
    bool exit_requested;
    /** @brief Terminal backend contract failure; stops all further Goat execution. */
    bool native_failed;
    /** @brief Host cancellation status; not the language exit atomic arbiter. */
    int exit_code;

    /** @brief A unique identifier for the process. */
    uint64_t id;

    /** @brief Pointer to the main thread of the process. */
    thread_t *main_thread;

    /** @brief List of objects managed by the process. */
    object_list_t objects;

    /** @brief Pool of integer objects managed by the process. */
    object_list_t integers;

    /** @brief Pool of real number objects managed by the process. */
    object_list_t real_numbers;

    /** @brief Pool of dynamic string objects managed by the process. */
    object_list_t dynamic_strings;

    /** @brief Pool of user-defined objects managed by the process. */
    object_list_t user_defined_objects;

    /** @brief Pool of character objects managed by the process. */
    object_list_t characters;

    /** @brief Cache of strings used during the execution of the process. */
    object_t **string_cache;

    /** @brief The size of the string cache. */
    size_t string_cache_size;

    /** @brief External objects kept alive by the process for the garbage collector. */
    object_t **gc_roots;

    /** @brief The number of registered external garbage-collection roots. */
    size_t gc_roots_size;
};

/** @brief Creates a new process. */
process_t *create_process();

/** @brief Destroys a process and frees all its resources, including threads and objects. */
void destroy_process(process_t *process);

/** @brief Set launch context exactly once, before execution. canonical_path is the
 * already resolved startup path (no filesystem access here). Retains module and
 * owns a UTF-32 directory value. Returns false for invalid/repeated preparation.
 */
bool prepare_process_launch(process_t *process,
                            module_t *module,
                            uint32_t file_id,
                            const char *canonical_path);

/** @brief Protect model operations shared by OS-backed VM threads. */
void process_execution_enter(process_t *process);
void process_execution_leave(process_t *process);
/** @brief Drop all execution-lock recursion before a blocking wait; restore exactly afterward. */
size_t process_execution_suspend(process_t *process);
void process_execution_resume(process_t *process, size_t depth);

/** @brief Common VM/native exit boundary. Range and dynamic initialization are checked
 * before process-wide arbitration. Rejected calls return an ordinary exception;
 * accepted calls never return and do not unwind or destroy live VM/native state.
 * The caller provides its own live thread context (under the model lock).
 */
operation_result_t request_program_exit(thread_t *thread, int64_t code);
