/** @file log10.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Runtime and abstract implementations of log10.
 */
#include "math_function.h"
#include "registry.h"

#include <math.h>

static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    return execute_unary_math(args, thread, log10);
}

static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    return interpret_unary_math(state, args, log10);
}

const builtin_function_t builtin_log10 = {.name = L"log10",
                                          .min_args = 1,
                                          .effects = BUILTIN_EFFECT_NONE,
                                          .execute = execute,
                                          .interpret = interpret,
                                          .get_object = get_function_log10};

object_t *get_function_log10(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_log10);
}
