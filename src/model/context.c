/**
 * @file context.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implements the creation, management, and destruction of execution contexts in the Goat
 * programming language.
 */

#include "context.h"

#include "lib/allocate.h"
#include "object.h"
#include "process.h"

object_t *get_this_key() {
    static object_static_string_t storage;
    return get_static_string_object(&storage, U"this");
}

context_t *
create_context_with_protos(process_t *process, context_t *caller, object_array_t protos) {
    context_t *ctx =
        create_context_with_protos_in_file(process,
                                           caller,
                                           protos,
                                           caller->module ? caller->module : process->module,
                                           caller->module ? caller->access.file_id : 0);
    if (caller->module) {
        add_lexical_scope(&ctx->access, caller->data);
        inherit_function_access(&ctx->access, &caller->access);
    }
    return ctx;
}

context_t *create_context_with_protos_in_module(process_t *process,
                                                context_t *caller,
                                                object_array_t protos,
                                                module_t *module) {
    return create_context_with_protos_in_file(process, caller, protos, module, 0);
}

context_t *create_context_with_protos_in_file(process_t *process,
                                              context_t *caller,
                                              object_array_t protos,
                                              module_t *module,
                                              uint32_t file_id) {
    context_t *ctx = (context_t *)ALLOC(sizeof(context_t));
    ctx->module = retain_module_identity(module);
    ctx->data = create_user_defined_object_in_file(process, protos, ctx->module, file_id);
    set_inheritance_prototype_count(ctx->data, 0);
    init_function_access_in_file(&ctx->access, module, file_id);
    add_function_access_domain(&ctx->access, get_object_access_domain(ctx->data));
    ctx->native_disabled = caller->native_disabled;
    ctx->file_initialization_boundary = false;
    ctx->iteration_index = 0;
    ctx->pending_flow = FLOW_NORMAL;
    ctx->pending_value = NULL;
    ctx->pending_address = BAD_INSTR_INDEX;
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
    clear_function_access(&context->access, true);
    /* The `this` property is a weak self-reference (see create_property), so releasing the
       context's reference does not leave a cycle behind and never mutates the data object,
       which may still be reachable from the program (e.g. returned from the call or captured
       by a closure). */
    DECREF(context->data);
    DECREFIF(context->pending_value);
    release_module_identity(context->module);
    context_t *previous = context->previous;
    FREE(context);
    return previous;
}
