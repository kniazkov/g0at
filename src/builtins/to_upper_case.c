/** @file to_upper_case.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Converts a string to uppercase.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "model/object.h"
#include "model/thread.h"
#include "registry.h"

#include <wchar.h>

/** @brief Runtime executor for the method form `string.toUpperCase()`. */
static operation_result_t
execute_method(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    (void)count;
    if (receiver->vtbl->type != TYPE_STRING)
        return operation_exception(get_exception_invalid_argument());
    string_value_t value = convert_object_to_string(receiver);
    if (value.length == 0) {
        FREE_STRING(value);
        INCREF(receiver);
        return operation_success(receiver);
    }
    size_t length = value.length;
    wchar_t *copy = ALLOC((length + 1) * sizeof(wchar_t));
    for (size_t i = 0; i < length; i++)
        copy[i] = unicode_towupper(value.data[i]);
    copy[length] = 0;
    FREE_STRING(value);
    return operation_success(
        create_string_object(thread->process, (string_value_t){copy, length, true}));
}

/** @brief Runtime executor for the plain call form `toUpperCase(string)`. */
static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    if (count < 1)
        return operation_exception(get_exception_invalid_argument());
    return execute_method(args[0], args + 1, (uint16_t)(count - 1), thread);
}

/** @brief Normal-result domain; does not execute runtime effects. */
static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    if (count < 1)
        return make_bottom_element();
    if (args[0]->type == LATTICE_BOTTOM)
        return make_bottom_element();
    if (args[0]->type == LATTICE_STRING_CONSTANT) {
        const string_constant_element_t *str = (const string_constant_element_t *)args[0];
        wchar_t *copy = alloc_from_arena(state->arena, (str->value.length + 1) * sizeof(wchar_t));
        for (size_t i = 0; i < str->value.length; i++)
            copy[i] = unicode_towupper(str->value.data[i]);
        copy[str->value.length] = 0;
        return make_string_constant_element(state->arena, (string_view_t){copy, str->value.length});
    }
    return make_string_element();
}

const builtin_function_t builtin_to_upper_case = {.name = L"toUpperCase",
                                                  .min_args = 0,
                                                  .effects = BUILTIN_EFFECT_NONE,
                                                  .execute = execute,
                                                  .execute_method = execute_method,
                                                  .interpret = interpret,
                                                  .get_object = get_function_to_upper_case};

object_t *get_function_to_upper_case(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_to_upper_case);
}
