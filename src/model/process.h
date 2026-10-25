/**
 * @file process.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the structure for processes in the Goat programming language.
 */

#pragma once

#include "object_list.h"

#include <stdint.h>

typedef struct process_t process_t;
typedef struct thread_t thread_t;
typedef struct module_t module_t;

/** @brief A process in Goat. */
struct process_t {
    /** @brief Owned identity of the single program module until module loading is added. */
    module_t *module;
    /** @brief Owned per-module file scopes/images, destroyed after managed objects. */
    struct module_files_t *module_files;
    /** @brief Borrowed top of the single-thread file initialization stack. */
    struct module_file_t *initializing_file;
    /** @brief Frozen direct-entry identity and shared directory, outside immutable images. */
    module_t *launch_module;
    uint32_t launch_file;       /**< Entry source index inside launch_module. */
    object_t *launch_directory; /**< Owned shared directory, also a GC root. */

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
