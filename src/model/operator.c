/** @file operator.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared operator lookup and post-call contracts.
 */
#include "operator.h"

#include "context.h"
#include "module_files.h"
#include "thread.h"

#include <assert.h>

operator_lookup_t
lookup_operator_handler(const function_access_t *access, object_t *receiver, object_t *key) {
    if (receiver->vtbl->type != TYPE_USER_DEFINED_OBJECT)
        return (operator_lookup_t){.status = OPERATOR_NOT_APPLICABLE};
    property_lookup_t found = lookup_accessible_property(access, receiver, key, true);
    if (found.status == MSTAT_PROPERTY_NOT_FOUND)
        return (operator_lookup_t){.status = OPERATOR_ABSENT};
    if (found.status == MSTAT_OK && is_function_object(found.value)) {
        INCREF(found.value);
        return (operator_lookup_t){.status = OPERATOR_FOUND, .function = found.value};
    }
    if (found.status == MSTAT_FAILED_FILE)
        return (operator_lookup_t){
            .status = OPERATOR_ERROR,
            .exception = retain_exception(module_file_exception(module_scope_file(found.storage)))};
    operation_result_t failure = found.status == MSTAT_OK
                                     ? operation_exception(get_exception_invalid_operation())
                                     : access_status_result(found.status, found.value);
    return (operator_lookup_t){.status = OPERATOR_ERROR,
                               .exception = {failure.value, retain_exception_trace(failure.trace)}};
}

void clear_operator_lookup(operator_lookup_t *lookup) {
    DECREFIF(lookup->function);
    clear_exception(&lookup->exception);
    *lookup = (operator_lookup_t){0};
}

bool complete_operator_result(thread_t *thread,
                              operator_result_policy_t policy,
                              instr_index_t origin,
                              instr_index_t resume) {
    object_t *value = pop_object_from_stack(thread->data_stack);
    if ((policy == OPERATOR_RESULT_BOOL || policy == OPERATOR_RESULT_NEGATED_BOOL)
        && value->vtbl->type != TYPE_BOOLEAN) {
        DECREF(value);
        clear_exception(&thread->exception);
        thread->exception = (exception_t){get_exception_invalid_operation(),
                                          capture_exception_trace(thread->context, origin)};
        thread->instr_id = origin;
        return false;
    }
    if (policy == OPERATOR_RESULT_DISCARD)
        DECREF(value);
    else if (policy == OPERATOR_RESULT_NEGATED_BOOL) {
        bool result = !get_object_boolean_value(value);
        DECREF(value);
        push_object_onto_stack(thread->data_stack, get_boolean_object(result));
    } else
        push_object_onto_stack(thread->data_stack, value);
    thread->instr_id = resume;
    return true;
}

bool call_operator_handler(object_t *function,
                           object_t *receiver,
                           uint16_t count,
                           operator_result_policy_t policy,
                           instr_index_t resume,
                           thread_t *thread) {
    context_t *caller = thread->context;
    instr_index_t origin = thread->instr_id;
    if (!call_function_with_operator_receiver(function, count, receiver, thread))
        return false;
    if (thread->context == caller)
        return complete_operator_result(thread, policy, origin, resume);
    context_t *callee = thread->context;
    assert(callee->previous == caller && callee->control_flow == FLOW_RETURN);
    callee->operator_call = true;
    callee->operator_policy = policy;
    callee->operator_origin = origin;
    callee->jump_address[0] = resume;
    return true;
}
