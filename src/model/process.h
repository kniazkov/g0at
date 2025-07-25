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

    /**
     * @brief Pool of integer objects managed by the process.
     * 
     * This list contains objects of type `object_dynamic_integer_t` that were removed by the
     * garbage collector. Instead of releasing their memory back to the heap, these objects are
     * stored here for reuse, allowing faster allocation of new integer objects.
     */
    object_list_t integers;

    /**
     * @brief Pool of real number objects managed by the process.
     * 
     * This list contains objects of type `object_dynamic_real_t` that were collected
     * by the garbage collector. Rather than immediately freeing their memory, these
     * objects are retained in this pool for potential reuse.
     */
    object_list_t real_numbers;

    /**
     * @brief Pool of dynamic string objects managed by the process.
     * 
     * This list contains objects of type `object_dynamic_string_t` that were removed by the
     * garbage collector. Instead of releasing their memory back to the heap, these objects
     * are stored here for reuse. Only the memory holding the string data is freed,
     * while the object itself remains in the pool, allowing faster allocation of new dynamic
     * string objects.
     */
    object_list_t dynamic_strings;

    /**
     * @brief Pool of user-defined objects managed by the process.
     * 
     * This list contains objects of type `object_user_defined_t` that were removed by the
     * garbage collector. When a user-defined object is cleaned up, its collection of child 
     * objects (key-value pairs) and list of prototypes are cleared. However, the object itself 
     * is retained in the pool for reuse, avoiding the overhead of allocating new memory for 
     * user-defined objects during future object creation.
     */
    object_list_t user_defined_objects;

    /**
     * @brief Cache of strings used during the execution of the process.
     * 
     * This array holds pointers to string objects that are created during the execution
     * of the process. When a string is created from bytecode data, it is stored in this cache.
     * If the same string is encountered again, it is retrieved from the cache instead of being
     * recreated.
     */
    object_t **string_cache;

    /**
     * @brief The size of the string cache.
     */
    size_t string_cache_size;
};

/**
 * @brief Creates a new process.
 * 
 * This function initializes a new process, sets its unique ID, initializes an empty object list, 
 * and creates the main thread for the process.
 * 
 * @return A pointer to the newly created process.
 */
process_t *create_process();

/**
 * @brief Destroys a process and frees all its resources, including threads and objects.
 * 
 * This function destroys all threads associated with the process, calls the `release` 
 * function for each object in the process's object list, and then deallocates the memory 
 * used by the process itself.
 * 
 * @param process The process to be destroyed.
 */
void destroy_process(process_t *process);
