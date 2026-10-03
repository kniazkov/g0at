/** @file abs.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Type-preserving absolute values at runtime and during analysis.
 */
#include "analysis/abstract_state.h"
#include "model/thread.h"
#include "numeric.h"
#include "registry.h"

#include <math.h>

/** @brief Matches the language's wrapping unary negation, including INT64_MIN. */
static int64_t absolute_integer(int64_t value) {
    return value < 0 ? subtract_int64_wrapping(0, value) : value;
}

static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    if (is_integer_object(args[0]))
        return operation_success(
            create_integer_object(thread->process,
                                  absolute_integer(get_object_integer_value(args[0]).value)));
    real_value_t real = get_object_real_value(args[0]);
    if (!real.has_value)
        return operation_exception(get_exception_invalid_argument());
    return operation_success(create_real_number_object(thread->process, fabs(real.value)));
}

static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    const lattice_element_t *value = args[0];
    if (value->type == LATTICE_INTEGER_CONSTANT)
        return make_integer_constant_element(
            state->arena,
            absolute_integer(((const integer_constant_element_t *)value)->value));
    if (is_integer_lattice_type(value->type))
        return make_integer_element();
    if (value->type == LATTICE_REAL_CONSTANT)
        return make_real_constant_element(state->arena,
                                          fabs(((const real_constant_element_t *)value)->value));
    if (value->type == LATTICE_REAL)
        return make_real_element();
    if (value->type == LATTICE_NUMERIC || builtin_unknown_type(value))
        return make_numeric_element();
    return make_bottom_element();
}

const builtin_function_t builtin_abs = {.name = L"abs",
                                        .min_args = 1,
                                        .effects = BUILTIN_EFFECT_NONE,
                                        .execute = execute,
                                        .interpret = interpret,
                                        .get_object = get_function_abs};

object_t *get_function_abs(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_abs);
}
