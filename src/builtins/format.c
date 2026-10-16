/** @file format.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Formats a string by replacing %0, %1, ... placeholders with arguments.
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

/** @brief Runtime executor for the method form `string.format(arg0, ...)`. */
static operation_result_t
execute_method(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    if (receiver->vtbl->type != TYPE_STRING)
        return operation_exception(get_exception_invalid_argument());
    string_value_t format = convert_object_to_string(receiver);
    string_builder_t builder;
    init_string_builder(&builder, format.length);
    size_t pos = 0;
    while (pos < format.length) {
        if (format.data[pos] == L'%' && pos + 1 < format.length && format.data[pos + 1] >= L'0'
            && format.data[pos + 1] <= L'9') {
            size_t index = 0;
            size_t end = pos + 1;
            bool index_overflow = false;
            while (end < format.length && format.data[end] >= L'0' && format.data[end] <= L'9') {
                size_t digit = (size_t)(format.data[end] - L'0');
                if (index > (SIZE_MAX - digit) / 10)
                    index_overflow = true;
                else if (!index_overflow)
                    index = index * 10 + digit;
                end++;
            }
            if (!index_overflow && index < count) {
                string_value_t arg_value = convert_object_to_string(args[index]);
                append_string_value(&builder, arg_value);
                FREE_STRING(arg_value);
            } else {
                append_substring(&builder, format.data + pos, end - pos);
            }
            pos = end;
        } else {
            append_char(&builder, format.data[pos]);
            pos++;
        }
    }
    FREE_STRING(format);
    string_value_t result = (string_value_t){builder.data, builder.length, builder.data != NULL};
    return operation_success(create_string_object(thread->process, result));
}

/** @brief Runtime executor for the plain call form `format(format, arg0, ...)`. */
static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    if (count < 1)
        return operation_exception(get_exception_invalid_argument());
    return execute_method(args[0], args + 1, (uint16_t)(count - 1), thread);
}

/** @brief Normal-result domain; does not execute runtime effects. */
static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    (void)state;
    if (count < 1)
        return make_bottom_element();
    if (args[0]->type == LATTICE_BOTTOM)
        return make_bottom_element();
    return make_string_element();
}

const builtin_function_t builtin_format = {.name = L"format",
                                           .min_args = 0,
                                           .effects = BUILTIN_EFFECT_NONE,
                                           .execute = execute,
                                           .execute_method = execute_method,
                                           .interpret = interpret,
                                           .get_object = get_function_format};

object_t *get_function_format(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_format);
}
