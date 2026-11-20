/**
 * @file process.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the structure for processes in the Goat programming language.
 *
 * This is a Goat runtime owner, distinct from an OS child-process handle. It ties
 * threads, roots, module file state and object pools to one execution lifetime.
 * Module snapshots must destroy this runtime before releasing immutable images
 * borrowed by its objects.
 * @see free_module_snapshot
 */

#pragma once

#include "lib/threads/os_thread.h"
#include "object_list.h"

#include <stdint.h>

typedef struct process_t process_t;
typedef struct thread_t thread_t;
typedef struct module_t module_t;
typedef struct gc_root_slot_t gc_root_slot_t;
struct gc_stats_t;

/** @brief Limits apply to retained Goat allocator payload per process, not RSS. */
#define GOAT_POOL_BYTE_LIMIT ((size_t)512 * 1024)
#define GOAT_POOL_TYPE_LIMIT ((size_t)1024)
#define GOAT_POOL_VECTOR_SLOT_LIMIT ((size_t)64)

/** @brief A process in Goat. */
struct process_t {
    /** @brief Short recursive model lock; never retained across user blocking waits. */
    os_mutex_t *execution_mutex;
    /** @brief One recursive initializer lock for all files in this VM. */
    os_mutex_t *initialization_mutex;
    /** @brief Program-thread completion notification, guarded by execution_mutex. */
    os_condition_t *threads_changed;
    thread_t *finished_workers; /**< Completed workers whose OS handle needs reaping. */
    size_t active_threads;      /**< Unfinished program executions, including descendants. */
    /** @brief Loader waiters, guarded by execution_mutex, for lifecycle diagnostics. */
    size_t initialization_waiters;
    struct vm_execution_t *execution; /**< Borrowed while the VM owns execution. */

    /** @brief Owned default program identity; loaded module file sets retain their own identities.
     */
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

    /** @brief Retained payload bytes across all five object pools. */
    size_t pooled_bytes;

    /** @brief Cache of strings used during the execution of the process. */
    object_t **string_cache;

    /** @brief The size of the string cache. */
    size_t string_cache_size;

    /** @brief Pending zero-reference graph objects under the execution lock. */
    object_t *pending_releases;

    /** @brief True while an outer last-owner release is draining the queue. */
    bool draining_releases;

    /** @brief External owning roots; slot indices survive storage growth. */
    gc_root_slot_t *gc_roots;
    size_t gc_roots_size;             /**< Number of active registrations, including duplicates. */
    size_t gc_roots_used;             /**< High-water number of slots initialized. */
    size_t gc_roots_capacity;         /**< Slots reserved in the allocation. */
    size_t gc_roots_free;             /**< One-based first free slot, or zero. */
    uint64_t gc_root_serial;          /**< Never-reused registration serial within the process. */
    struct gc_stats_t *gc_statistics; /**< Optional host-owned diagnostics, normally NULL. */
    bool gc_run_end_observation;      /**< Tags the preexisting automatic end-of-run call only. */
    void *gc_session; /**< Active scoped collection protocol; protected by execution_mutex. */
    bool gc_collecting;               /**< Protects the current tracing/sweep pass from reentry. */
    bool destroying; /**< Teardown allows unregister but forbids registration and GC. */
};

/** @brief Creates a new process. */
process_t *create_process();

/** @brief Enqueues a DYING graph object and drains at the outermost release boundary. */
void enqueue_last_ref_release(object_t *obj);

/** @brief Try to admit a detached, already cleaned shell under both pool limits. */
bool try_pool_object(object_list_t *pool, object_t *object, size_t bytes);

/** @brief Remove a pooled shell for reuse or destruction and subtract its full payload. */
void remove_pooled_object(object_list_t *pool, object_t *object, size_t bytes);

/** @brief Drop all inactive pooled shells; returns allocator payload bytes released.
 * Requires the process execution lock or exclusive access. Never touches live objects,
 * runs collection, or allocates. An empty trim returns zero.
 */
size_t trim_process_object_pools(process_t *process);

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
