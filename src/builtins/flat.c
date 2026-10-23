/** @file flat.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Shallow mutable property materialization in ordinary lookup order.
 */
#include "analysis/lattice.h"
#include "model/builtin_function.h"
#include "model/context.h"
#include "model/thread.h"

static operation_result_t
execute_method(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    return operation_success(
        flat_object_with_access(thread->process, receiver, &thread->context->access));
}

static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    return count ? operation_success(
                       flat_object_with_access(thread->process, args[0], &thread->context->access))
                 : operation_exception(get_exception_invalid_argument());
}

static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    return count ? make_top_element() : make_bottom_element();
}

static const builtin_function_t builtin_flat = {.name = U"flat",
                                                .min_args = 0,
                                                .effects = BUILTIN_EFFECT_NONE,
                                                .execute = execute,
                                                .execute_method = execute_method,
                                                .interpret = interpret,
                                                .get_object = get_function_flat};

object_t *get_function_flat(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_flat);
}
