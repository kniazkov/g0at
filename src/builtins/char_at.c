/** @file char_at.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Returns the character at the given index in a string.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "lib/allocate.h"
#include "model/object.h"
#include "model/thread.h"
#include "registry.h"

#include <wchar.h>

/** @brief Runtime executor for the method form `string.charAt(index)`. */
static operation_result_t
execute_method(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    if (count < 1)
        return operation_exception(get_exception_invalid_argument());
    if (receiver->vtbl->type != TYPE_STRING)
        return operation_exception(get_exception_invalid_argument());
    if (!is_integer_object(args[0]))
        return operation_exception(get_exception_invalid_argument());
    string_value_t value = convert_object_to_string(receiver);
    int64_t index = get_object_integer_value(args[0]).value;
    if (index < 0 || (size_t)index >= value.length) {
        FREE_STRING(value);
        return operation_exception(get_exception_invalid_argument());
    }
    wchar_t ch = value.data[index];
    FREE_STRING(value);
    return operation_success(create_character_object(thread->process, (uint32_t)ch));
}

/** @brief Runtime executor for the plain call form `charAt(string, index)`. */
static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    if (count < 2)
        return operation_exception(get_exception_invalid_argument());
    return execute_method(args[0], args + 1, (uint16_t)(count - 1), thread);
}

/** @brief Normal-result domain; does not execute runtime effects. */
static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    if (count < 2)
        return make_bottom_element();
    const lattice_element_t *receiver = args[0];
    if (receiver->type == LATTICE_BOTTOM || args[1]->type == LATTICE_BOTTOM)
        return make_bottom_element();
    if (receiver->type == LATTICE_STRING_CONSTANT && args[1]->type == LATTICE_INTEGER_CONSTANT) {
        const string_constant_element_t *str = (const string_constant_element_t *)receiver;
        int64_t index = ((const integer_constant_element_t *)args[1])->value;
        if (index >= 0 && (size_t)index < str->value.length)
            return make_char_constant_element(state->arena, (uint32_t)str->value.data[index]);
        return make_bottom_element();
    }
    return make_char_element();
}

const builtin_function_t builtin_char_at = {.name = L"charAt",
                                            .min_args = 1,
                                            .effects = BUILTIN_EFFECT_NONE,
                                            .execute = execute,
                                            .execute_method = execute_method,
                                            .interpret = interpret,
                                            .get_object = get_function_char_at};

object_t *get_function_char_at(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_char_at);
}
