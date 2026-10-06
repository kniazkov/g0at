/** @file atan.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Runtime and abstract implementations of atan.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "model/thread.h"
#include "numeric.h"
#include "registry.h"

#include <math.h>

/** @brief Runtime executor; the shared caller has checked minimum arity. */
static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    real_value_t y = get_object_real_value(args[0]);
    if (!y.has_value) {
        return operation_exception(get_exception_invalid_argument());
    }
    if (count < 2) {
        return operation_success(create_real_number_object(thread->process, atan(y.value)));
    }
    real_value_t x = get_object_real_value(args[1]);
    if (!x.has_value) {
        return operation_exception(get_exception_invalid_argument());
    }
    return operation_success(create_real_number_object(thread->process, atan2(y.value, x.value)));
}

/** @brief Normal-result domain; does not execute runtime effects. */
static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    size_t arity = count < 2 ? 1 : 2;
    for (size_t i = 0; i < arity; i++) {
        if (!is_numeric_lattice_type(args[i]->type) && !builtin_unknown_type(args[i]))
            return make_bottom_element();
    }
    if (arity == 1) {
        double y;
        if (builtin_numeric_constant(args[0], &y))
            return make_real_constant_element(state->arena, atan(y));
        return make_real_element();
    }
    double y, x;
    if (builtin_numeric_constant(args[0], &y) && builtin_numeric_constant(args[1], &x))
        return make_real_constant_element(state->arena, atan2(y, x));
    return make_real_element();
}

const builtin_function_t builtin_atan = {.name = L"atan",
                                         .min_args = 1,
                                         .effects = BUILTIN_EFFECT_NONE,
                                         .execute = execute,
                                         .interpret = interpret,
                                         .get_object = get_function_atan};

object_t *get_function_atan(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_atan);
}
