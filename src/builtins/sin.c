/** @file sin.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Runtime and abstract implementations of sin.
 */
#include "math_function.h"
#include "registry.h"

#include <math.h>

static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    return execute_unary_math(args, thread, sin);
}

static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    return interpret_unary_math(state, args, sin);
}

const builtin_function_t builtin_sin = {.name = L"sin",
                                        .min_args = 1,
                                        .effects = BUILTIN_EFFECT_NONE,
                                        .execute = execute,
                                        .interpret = interpret,
                                        .get_object = get_function_sin};

object_t *get_function_sin(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_sin);
}
