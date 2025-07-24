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
#include "common/types.h"

/**
 * @typedef process_t
 * @brief Forward declaration for the process structure.
 */
typedef struct process_t process_t;

/**
 * @typedef context_t
 * @brief Forward declaration for the context structure.
 */
typedef struct context_t context_t;

/**
 * @typedef thread_t
 * @brief Forward declaration for the thread structure.
 */
typedef struct thread_t thread_t;

/**
 * @def ARGS_CAPACITY
 * @brief Defines the maximum number of arguments that can be stored in the argument array.
 * 
 * This macro sets the maximum number of arguments that can be stored in the `args` array of
 * a thread during the execution of instructions.
 */
#define ARGS_CAPACITY 3

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
     * @brief The process that owns this thread.
     * 
     * Each thread belongs to a process. This pointer allows the thread to interact with its
     * parent process, access shared resources, and be managed by the process.
     */
    process_t *process;

    /**
     * @brief The previous thread in the circular linked list.
     * 
     * This pointer references the thread that precedes the current thread in the circular list
     * of threads within the process. If the thread is the only one in the process, it points
     * to itself.
     */
    thread_t *previous;

    /**
     * @brief The next thread in the circular linked list.
     * 
     * This pointer references the thread that follows the current thread in the circular list.
     * If the thread is the only one in the process, it points to itself.
     */
    thread_t *next;

    /**
     * @brief The current execution context of the thread.
     * 
     * This pointer references the `context_t` structure that represents the current 
     * execution context of the thread. The context is used to maintain the execution state of the 
     * thread, allowing the system to manage execution flow, function calls, and variable scopes.
     */
    context_t *context;

    /**
     * @brief The data stack used by the thread.
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
    instr_index_t instr_id;

    /**
     * @brief Array of arguments used during instruction execution.
     * 
     * This array stores the arguments pushed onto the argument stack during the execution of
     * instructions. This is used for `ARG` opcode that pushing additional arguments beyond
     * the standard two available in each instruction.
     */
    uint32_t args[ARGS_CAPACITY];

    /**
     * @brief The number of arguments currently stored in the `args` array.
     * 
     * This field keeps track of how many arguments have been pushed onto the `args` array during
     * the execution of instructions. 
     */
    int args_count;
};

/**
 * @brief Creates a new thread within the given process with an initial context.
 * 
 * This function initializes a new thread for the specified process and sets up the execution
 * context for the thread. The thread is added to the process's circular linked list of threads.
 * It also initializes the data stack for the thread and sets the instruction pointer to the
 * starting position. The initial context is set for the thread, which holds the 
 * data and control information needed for execution.
 * 
 * @param process The process to which the new thread will belong.
 * @param context The execution context to be assigned to the new thread.
 * @return A pointer to the newly created thread.
 */
thread_t *create_thread(process_t *process, context_t *context);

/**
 * @brief Destroys a thread and frees its resources.
 * 
 * This function cleans up the memory used by a thread. It removes the thread from the
 * process's thread list, destroys its data stack, and frees the memory allocated for the thread.
 * 
 * @param thread The thread to destroy.
 */
void destroy_thread(thread_t *thread);