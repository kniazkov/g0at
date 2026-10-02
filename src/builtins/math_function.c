/** @file math_function.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Numeric native execution and abstract domains.
 */
#include "math_function.h"

#include "model/thread.h"
#include "numeric.h"

operation_result_t execute_unary_math(object_t **args, thread_t *thread, unary_math_t function) {
    real_value_t a = get_object_real_value(args[0]);
    if (!a.has_value)
        return operation_exception(get_exception_invalid_argument());
    return operation_success(create_real_number_object(thread->process, function(a.value)));
}

operation_result_t execute_binary_math(object_t **args, thread_t *thread, binary_math_t function) {
    real_value_t a = get_object_real_value(args[0]);
    real_value_t b = get_object_real_value(args[1]);
    if (!a.has_value || !b.has_value)
        return operation_exception(get_exception_invalid_argument());
    return operation_success(
        create_real_number_object(thread->process, function(a.value, b.value)));
}

const lattice_element_t *interpret_unary_math(abstract_state_t *state,
                                              const lattice_element_t *const *args,
                                              unary_math_t function) {
    double a;
    if (builtin_numeric_constant(args[0], &a))
        return make_real_constant_element(state->arena, function(a));
    if (is_numeric_lattice_type(args[0]->type) || builtin_unknown_type(args[0]))
        return make_real_element();
    return make_bottom_element();
}

const lattice_element_t *interpret_binary_math(abstract_state_t *state,
                                               const lattice_element_t *const *args,
                                               binary_math_t function) {
    for (size_t i = 0; i < 2; i++) {
        if (!is_numeric_lattice_type(args[i]->type) && !builtin_unknown_type(args[i]))
            return make_bottom_element();
    }
    double a, b;
    if (builtin_numeric_constant(args[0], &a) && builtin_numeric_constant(args[1], &b))
        return make_real_constant_element(state->arena, function(a, b));
    return make_real_element();
}
