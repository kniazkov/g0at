/** @file sign.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Runtime and abstract implementations of sign.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "model/thread.h"
#include "numeric.h"
#include "registry.h"

#include <math.h>

/** @brief Runtime executor; the shared caller has checked minimum arity. */
static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    real_value_t argument = get_object_real_value(args[0]);
    if (!argument.has_value)
        return operation_exception(get_exception_invalid_argument());
    double value = argument.value;
    int sign;
    if (value > 0) {
        sign = 1;
    } else if (value < 0) {
        sign = -1;
    } else {
        sign = 0;
    }
    return operation_success(get_static_integer_object(sign));
}

/** @brief Normal-result domain; does not execute runtime effects. */
static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    double value;
    if (builtin_numeric_constant(args[0], &value))
        return make_integer_constant_element(state->arena, value > 0 ? 1 : value < 0 ? -1 : 0);
    if (args[0]->type == LATTICE_INTEGER_RANGE) {
        const integer_range_element_t *range = (const integer_range_element_t *)args[0];
        return make_integer_range_element(state->arena,
                                          range->min > 0   ? 1
                                          : range->min < 0 ? -1
                                                           : 0,
                                          range->max > 0   ? 1
                                          : range->max < 0 ? -1
                                                           : 0);
    }
    if (is_numeric_lattice_type(args[0]->type) || builtin_unknown_type(args[0]))
        return make_integer_range_element(state->arena, -1, 1);
    return make_bottom_element();
}

const builtin_function_t builtin_sign = {.name = L"sign",
                                         .min_args = 1,
                                         .effects = BUILTIN_EFFECT_NONE,
                                         .execute = execute,
                                         .interpret = interpret,
                                         .get_object = get_function_sign};

object_t *get_function_sign(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_sign);
}
