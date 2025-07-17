/**
 * @file context.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implements the creation, management, and destruction of execution contexts
 *  in the Goat programming language.
 * 
 * This file contains the implementations for functions related to managing contexts
 * in Goat. Contexts are a core concept used to encapsulate the execution state of a program,
 * allowing for structured program execution and flow control.
 */

#include "context.h"
#include "object.h"
#include "lib/allocate.h"

context_t *create_context(process_t *process, context_t *caller, object_t *proto) {
    context_t *ctx = (context_t *)ALLOC(sizeof(context_t));
    ctx->data = create_user_defined_object(
        process,
        (object_array_t){ proto ? &proto : &caller->data, 1 }
    );
    ctx->previous = caller;
    ctx->ret_address = BAD_INSTR_INDEX;
    ctx->ret_value_index = caller->ret_value_index;
    ctx->unwinding_index = BAD_STACK_INDEX;
    return ctx;
}

context_t *destroy_context(context_t *context) {
    DECREF(context->data);
    context_t *previous = context->previous;
    FREE(context);
    return previous;
}
