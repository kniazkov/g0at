/** @file round.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Runtime and abstract implementations of round.
 */
#include "math_function.h"
#include "registry.h"

#include <math.h>

static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    return execute_unary_math(args, thread, round);
}

static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    return interpret_unary_math(state, args, round);
}

const builtin_function_t builtin_round = {.name = L"round",
                                          .min_args = 1,
                                          .effects = BUILTIN_EFFECT_NONE,
                                          .execute = execute,
                                          .interpret = interpret,
                                          .get_object = get_function_round};

object_t *get_function_round(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_round);
}
