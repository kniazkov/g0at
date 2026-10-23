/** @file str.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Converts any object to its string representation.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "goat/unicode.h"
#include "model/context.h"
#include "model/object.h"
#include "model/thread.h"
#include "registry.h"

/** @brief Runtime executor for the plain call form `str(object)`. */
static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    if (count < 1)
        return operation_exception(get_exception_invalid_argument());
    string_value_t value = object_to_string_with_access(args[0], &thread->context->access);
    return operation_success(create_string_object(thread->process, value));
}

/** @brief Normal-result domain; does not execute runtime effects. */
static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    if (count < 1)
        return make_bottom_element();
    const lattice_element_t *arg = args[0];
    if (arg->type == LATTICE_BOTTOM)
        return make_bottom_element();
    if (arg->type == LATTICE_STRING_CONSTANT)
        return make_string_constant_element(state->arena,
                                            ((const string_constant_element_t *)arg)->value);
    return make_string_element();
}

const builtin_function_t builtin_str = {.name = U"str",
                                        .min_args = 1,
                                        .effects = BUILTIN_EFFECT_NONE,
                                        .execute = execute,
                                        .interpret = interpret,
                                        .get_object = get_function_str};

object_t *get_function_str(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_str);
}
