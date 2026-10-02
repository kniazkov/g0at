/** @file sqrt.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Runtime and abstract implementations of sqrt.
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
    double result = sqrt(value);
    return operation_success(create_real_number_object(thread->process, result));
}

/** @brief Normal-result domain; does not execute runtime effects. */
static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    double value;
    if (builtin_numeric_constant(args[0], &value))
        return make_real_constant_element(state->arena, sqrt(value));
    if (is_numeric_lattice_type(args[0]->type) || builtin_unknown_type(args[0]))
        return make_real_element();
    return make_bottom_element();
}

const builtin_function_t builtin_sqrt = {.name = L"sqrt",
                                         .min_args = 1,
                                         .effects = BUILTIN_EFFECT_NONE,
                                         .execute = execute,
                                         .interpret = interpret,
                                         .get_object = get_function_sqrt};

object_t *get_function_sqrt(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_sqrt);
}
