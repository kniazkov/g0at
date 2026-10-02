/**
 * @file context.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implements the creation, management, and destruction of execution contexts in the Goat
 * programming language.
 */

#include "context.h"

#include "lib/allocate.h"
#include "object.h"

context_t *create_context(process_t *process, context_t *caller, object_t *proto) {
    context_t *ctx = (context_t *)ALLOC(sizeof(context_t));
    ctx->data =
        create_user_defined_object(process, (object_array_t){proto ? &proto : &caller->data, 1});
    ctx->previous = caller;
    ctx->control_flow = FLOW_NORMAL;
    ctx->jump_address[0] = BAD_INSTR_INDEX;
    ctx->jump_address[1] = BAD_INSTR_INDEX;
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
