/**
 * @file thread.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines the structure for threads in the Goat programming language.
 * 
 * A thread represents a unit of execution within a process in the Goat language.
 * Each thread has a reference to its parent process and is part of a circular linked list
 * of threads. A thread also contains a stack for data storage and an index to track the current
 * instruction being executed. Threads are used by the virtual machine, which operates
 * in a stack-based manner.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "object_stack.h"

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
 * @typedef instr_index_t
 * @brief Type for indexing the instruction list.
 * 
 * The instruction index is used to keep track of the current instruction that the thread
 * is executing.
 */
typedef size_t instr_index_t;

/**
 * @struct thread_t
 * @brief Represents a thread in Goat.
 * 
 * A thread is a unit of execution within a process. It has its own execution context, including
 * a data stack and a pointer to the current instruction in the instruction list. Threads are
 * organized in a circular linked list. Each thread belongs to a specific process and has
 * a unique identifier.
 */
struct thread_t {
    /**
     * @brief A unique identifier for the thread.
     * 
     * This field stores the unique identifier for the thread, allowing the system to track
     * and manage individual threads within the process.
     */
    uint64_t id;

    /**
     * @brief Pointer to the process that owns this thread.
     * 
     * Each thread belongs to a process. This pointer allows the thread to interact with its
     * parent process, access shared resources, and be managed by the process.
     */
    process_t *process;

    /**
     * @brief Pointer to the previous thread in the circular linked list.
     * 
     * This pointer references the thread that precedes the current thread in the circular list
     * of threads within the process. If the thread is the only one in the process, it points
     * to itself.
     */
    thread_t *previous;

    /**
     * @brief Pointer to the next thread in the circular linked list.
     * 
     * This pointer references the thread that follows the current thread in the circular list.
     * If the thread is the only one in the process, it points to itself.
     */
    thread_t *next;

    /**
     * @brief Pointer to the data stack used by the thread.
     * 
     * The data stack is used by the virtual machine to store temporary data during execution,
     * such as local variables, function arguments, and return addresses. It supports the
     * stack-based execution model of the VM.
     */
    object_stack_t *data_stack;

    /**
     * @brief Index of the current instruction being executed by the thread.
     * 
     * The current_instruction index points to the current instruction in the list of instructions
     * to be executed by the virtual machine. The thread uses this index to fetch and execute
     * instructions sequentially.
     */
    instr_index_t current_instruction;
};
