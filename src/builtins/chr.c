/** @file chr.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Returns the character with the given Unicode code point.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "model/thread.h"
#include "registry.h"

/** @brief Runtime executor; the shared caller has checked minimum arity. */
static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    if (!is_integer_object(args[0]))
        return operation_exception(get_exception_invalid_argument());
    return operation_success(
        create_character_object(thread->process,
                                (uint32_t)get_object_integer_value(args[0]).value));
}

/** @brief Normal-result domain; does not execute runtime effects. */
static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    const lattice_element_t *arg = args[0];
    if (arg->type == LATTICE_INTEGER_CONSTANT)
        return make_char_constant_element(
            state->arena,
            (uint32_t)((const integer_constant_element_t *)arg)->value);
    if (arg->type == LATTICE_BOTTOM)
        return arg;
    return make_char_element();
}

const builtin_function_t builtin_chr = {.name = L"chr",
                                        .min_args = 1,
                                        .effects = BUILTIN_EFFECT_NONE,
                                        .execute = execute,
                                        .interpret = interpret,
                                        .get_object = get_function_chr};

object_t *get_function_chr(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_chr);
}
