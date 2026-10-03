/**
 * @file thread.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the structure for threads in the Goat programming language.
 */

#pragma once

#include "common/types.h"
#include "exception.h"
#include "object_stack.h"

#include <stddef.h>
#include <stdint.h>

typedef struct process_t process_t;
typedef struct context_t context_t;
typedef struct thread_t thread_t;

/** @brief Defines the maximum number of arguments that can be stored in the argument array. */
#define ARGS_CAPACITY 3

/** @brief A thread in Goat. */
struct thread_t {
    /** @brief A unique identifier for the thread. */
    uint64_t id;

    /** @brief Owning process; provides access to shared objects. */
    process_t *process;

    /** @brief The previous thread in the circular linked list. */
    thread_t *previous;

    /** @brief The next thread in the circular linked list. */
    thread_t *next;

    /** @brief The current execution context of the thread. */
    context_t *context;

    /** @brief Pending native-call or unhandled exception, owned and traced by GC. */
    exception_t exception;

    /** @brief The data stack used by the thread. */
    object_stack_t *data_stack;

    /** @brief Index of the current instruction being executed by the thread. */
    instr_index_t instr_id;

    /** @brief Array of arguments used during instruction execution. */
    uint32_t args[ARGS_CAPACITY];

    /** @brief The number of arguments currently stored in the `args` array. */
    int args_count;
};

/** @brief Creates a new thread within the given process with an initial context. */
thread_t *create_thread(process_t *process, context_t *context);

/** @brief Destroys a thread and frees its resources. */
void destroy_thread(thread_t *thread);
