/**
 * @file context.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implements the creation, management, and destruction of execution contexts in the Goat
 * programming language.
 */

#include "context.h"

#include "lib/allocate.h"
#include "object.h"

object_t *get_this_key() {
    static object_static_string_t storage;
    return get_static_string_object(&storage, L"this");
}

context_t *
create_context_with_protos(process_t *process, context_t *caller, object_array_t protos) {
    context_t *ctx = (context_t *)ALLOC(sizeof(context_t));
    ctx->data = create_user_defined_object(process, protos);
    ctx->native_disabled = caller->native_disabled;
    ctx->previous = caller;
    ctx->control_flow = FLOW_NORMAL;
    ctx->jump_address[0] = BAD_INSTR_INDEX;
    ctx->jump_address[1] = BAD_INSTR_INDEX;
    ctx->ret_value_index = caller->ret_value_index;
    ctx->unwinding_index = BAD_STACK_INDEX;
    return ctx;
}

context_t *create_context(process_t *process, context_t *caller, object_t *proto) {
    return create_context_with_protos(process,
                                      caller,
                                      (object_array_t){proto ? &proto : &caller->data, 1});
}

context_t *destroy_context(context_t *context) {
    /* The `this` property points back to the data object itself. Replace it with null so the
       strong self-reference is released and the object can be freed by reference counting. */
    replace_object_property_value(context->data, get_this_key(), get_null_object());
    DECREF(context->data);
    context_t *previous = context->previous;
    FREE(context);
    return previous;
}
