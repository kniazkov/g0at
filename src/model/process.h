/**
 * @file process.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the structure for processes in the Goat programming language.
 */

#pragma once

#include <stdint.h>

#include "object_list.h"

typedef struct process_t process_t;

typedef struct thread_t thread_t;

/** @brief A process in Goat. */
struct process_t {
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

    /** @brief Cache of strings used during the execution of the process. */
    object_t **string_cache;

    /** @brief The size of the string cache. */
    size_t string_cache_size;
};

/** @brief Creates a new process. */
process_t *create_process();

/** @brief Destroys a process and frees all its resources, including threads and objects. */
void destroy_process(process_t *process);
