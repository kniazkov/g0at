/**
 * @file context.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the structure and behavior for the execution context in the Goat programming
 * language.
 */

#pragma once

#include "common/control_flow.h"
#include "common/types.h"

typedef struct object_t object_t;

typedef struct context_t context_t;

typedef struct process_t process_t;

/** @brief The execution context in the Goat programming language. */
struct context_t {
    /** @brief A data associated with the current execution context. */
    object_t *data;

    /** @brief A pointer to the previous context in the stack. */
    context_t *previous;

    /**
     * @brief Control-flow mode handled by this context.
     *
     * Defines which non-local control-flow transfer stops unwinding at this context.
     */
    control_flow_t control_flow;

    /**
     * @brief Jump targets used when unwinding this context.
     *
     * For example, a function-call context uses the `FLOW_RETURN` target to resume execution after
     * the call.
     */
    instr_index_t jump_address[2];

    /**
     * @brief Stack index for the return value of a function.
     *
     * Before a function call, a placeholder (e.g., null) is pushed onto the stack at this index.
     * When the function returns via the RET opcode, the placeholder is replaced with the actual
     * return value, making it accessible to the caller.
     */
    stack_index_t ret_value_index;

    /**
     * @brief Stack index to unwind to after exiting the context.
     *
     * The stack size before entering the current context.
     */
    stack_index_t unwinding_index;
};

/** @brief Retrieves the singleton instance of the root context. */
context_t *get_root_context();

/** @brief Creates a context linked to caller; NULL proto uses the caller data as prototype. */
context_t *create_context(process_t *process, context_t *caller, object_t *proto);

/** @brief Releases the context and its data reference; returns the previous context. */
context_t *destroy_context(context_t *context);
