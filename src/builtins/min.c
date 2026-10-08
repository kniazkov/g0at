/** @file min.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Runtime and abstract implementations of min.
 */
#include "math_function.h"
#include "numeric.h"
#include "registry.h"

#include <math.h>

static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    return execute_binary_math(args, thread, builtin_minimum);
}

static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    return interpret_binary_math(state, args, builtin_minimum);
}

const builtin_function_t builtin_min = {.name = L"min",
                                        .min_args = 2,
                                        .effects = BUILTIN_EFFECT_NONE,
                                        .execute = execute,
                                        .interpret = interpret,
                                        .get_object = get_function_min};

object_t *get_function_min(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_min);
}
