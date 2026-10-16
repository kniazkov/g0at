/** @file repeat.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Repeats a string a given number of times.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "model/object.h"
#include "model/thread.h"
#include "registry.h"

#include <stdint.h>
#include <wchar.h>

/** @brief Runtime executor for the method form `string.repeat(count)`. */
static operation_result_t
execute_method(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    if (count < 1)
        return operation_exception(get_exception_invalid_argument());
    if (receiver->vtbl->type != TYPE_STRING)
        return operation_exception(get_exception_invalid_argument());
    if (!is_integer_object(args[0]))
        return operation_exception(get_exception_invalid_argument());
    int64_t repeat_count = get_object_integer_value(args[0]).value;
    if (repeat_count < 0)
        return operation_exception(get_exception_invalid_argument());
    string_value_t value = convert_object_to_string(receiver);
    if (repeat_count == 0 || value.length == 0) {
        FREE_STRING(value);
        if (repeat_count == 0)
            return operation_success(get_empty_string());
        INCREF(receiver);
        return operation_success(receiver);
    }
    size_t count_value = (size_t)repeat_count;
    if (count_value > SIZE_MAX / value.length) {
        FREE_STRING(value);
        return operation_exception(get_exception_invalid_argument());
    }
    size_t total = count_value * value.length;
    if (total > SIZE_MAX / sizeof(wchar_t) - 1) {
        FREE_STRING(value);
        return operation_exception(get_exception_invalid_argument());
    }
    string_builder_t builder;
    init_string_builder(&builder, total);
    for (size_t i = 0; i < count_value; i++)
        append_string_value(&builder, value);
    FREE_STRING(value);
    string_value_t result = (string_value_t){builder.data, builder.length, builder.data != NULL};
    return operation_success(create_string_object(thread->process, result));
}

/** @brief Runtime executor for the plain call form `repeat(string, count)`. */
static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    if (count < 2)
        return operation_exception(get_exception_invalid_argument());
    return execute_method(args[0], args + 1, (uint16_t)(count - 1), thread);
}

/** @brief Normal-result domain; does not execute runtime effects. */
static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    (void)state;
    if (count < 2)
        return make_bottom_element();
    if (args[0]->type == LATTICE_BOTTOM || args[1]->type == LATTICE_BOTTOM)
        return make_bottom_element();
    return make_string_element();
}

const builtin_function_t builtin_repeat = {.name = L"repeat",
                                           .min_args = 1,
                                           .effects = BUILTIN_EFFECT_NONE,
                                           .execute = execute,
                                           .execute_method = execute_method,
                                           .interpret = interpret,
                                           .get_object = get_function_repeat};

object_t *get_function_repeat(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_repeat);
}
