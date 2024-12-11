/**
 * @file process.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines the structure for processes in the Goat programming language.
 * 
 * A process represents an executing program within the Goat language.
 */

#pragma once

#include <stdint.h>

#include "object_list.h"

/**
 * @typedef process_t
 * @brief Forward declaration for the process structure.
 */
typedef struct process_t process_t;

/**
 * @typedef thread_t
 * @brief Forward declaration for the thread structure.
 */
typedef struct thread_t thread_t;

/**
 * @struct process_t
 * @brief Represents a process in Goat.
 * 
 * A process is an executing program within the Goat language. Each process has an isolated set
 * of data, which is represented as objects. These objects are stored in an object list, which is
 * also used by the garbage collector to manage the lifecycle of objects and perform
 * memory cleanup.
 * 
 * A process has at least one thread, and threads are organized in a circular linked list.
 * The first thread created during the process's execution is referred to as the main thread.
 */
struct process_t {
    /**
     * @brief A unique identifier for the process.
     * 
     * This field stores the unique identifier for the process, allowing the system to track
     * and manage individual processes.
     */
    uint64_t id;

    /**
     * @brief Pointer to the main thread of the process.
     * 
     * The main thread is the first thread created when the process is initialized. This thread
     * is responsible for starting the execution of the program.
     */
    thread_t *main_thread;

    /**
     * @brief List of objects managed by the process.
     * 
     * This list contains all the objects created by the process. The garbage collector
     * uses this list to identify and clean up unused objects.
     */
    object_list_t objects;
};
