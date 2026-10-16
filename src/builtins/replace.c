/** @file replace.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Replaces all occurrences of a substring with another substring.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "model/object.h"
#include "model/thread.h"
#include "registry.h"

#include <wchar.h>

/** @brief Runtime executor for the method form `string.replace(old, new)`. */
static operation_result_t
execute_method(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    if (count < 2)
        return operation_exception(get_exception_invalid_argument());
    if (receiver->vtbl->type != TYPE_STRING)
        return operation_exception(get_exception_invalid_argument());
    if ((args[0]->vtbl->type != TYPE_STRING && args[0]->vtbl->type != TYPE_CHARACTER)
        || (args[1]->vtbl->type != TYPE_STRING && args[1]->vtbl->type != TYPE_CHARACTER))
        return operation_exception(get_exception_invalid_argument());
    string_value_t value = convert_object_to_string(receiver);
    string_value_t old_part = convert_object_to_string(args[0]);
    string_value_t new_part = convert_object_to_string(args[1]);
    if (old_part.length == 0) {
        FREE_STRING(value);
        FREE_STRING(old_part);
        FREE_STRING(new_part);
        INCREF(receiver);
        return operation_success(receiver);
    }
    string_builder_t builder;
    init_string_builder(&builder, value.length);
    size_t pos = 0;
    while (pos < value.length) {
        if (pos + old_part.length <= value.length
            && !wmemcmp(value.data + pos, old_part.data, old_part.length)) {
            append_string_value(&builder, new_part);
            pos += old_part.length;
        } else {
            append_char(&builder, value.data[pos]);
            pos++;
        }
    }
    FREE_STRING(value);
    FREE_STRING(old_part);
    FREE_STRING(new_part);
    string_value_t result = (string_value_t){builder.data, builder.length, builder.data != NULL};
    return operation_success(create_string_object(thread->process, result));
}

/** @brief Runtime executor for the plain call form `replace(string, old, new)`. */
static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    if (count < 3)
        return operation_exception(get_exception_invalid_argument());
    return execute_method(args[0], args + 1, (uint16_t)(count - 1), thread);
}

/** @brief Normal-result domain; does not execute runtime effects. */
static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    if (count < 3)
        return make_bottom_element();
    if (args[0]->type == LATTICE_BOTTOM || args[1]->type == LATTICE_BOTTOM
        || args[2]->type == LATTICE_BOTTOM)
        return make_bottom_element();
    if (args[0]->type == LATTICE_STRING_CONSTANT) {
        const string_constant_element_t *str = (const string_constant_element_t *)args[0];
        string_view_t old_part;
        string_view_t new_part;
        if (get_string_view_from_constant(state->arena, args[1], &old_part)
            && get_string_view_from_constant(state->arena, args[2], &new_part)) {
            if (old_part.length == 0)
                return args[0];
            string_builder_t builder;
            init_string_builder(&builder, str->value.length);
            size_t pos = 0;
            while (pos < str->value.length) {
                if (pos + old_part.length <= str->value.length
                    && !wmemcmp(str->value.data + pos, old_part.data, old_part.length)) {
                    append_string_view(&builder, new_part);
                    pos += old_part.length;
                } else {
                    append_char(&builder, str->value.data[pos]);
                    pos++;
                }
            }
            string_view_t result = copy_string_to_arena(state->arena, builder.data, builder.length);
            if (builder.data)
                FREE(builder.data);
            return make_string_constant_element(state->arena, result);
        }
    }
    return make_string_element();
}

const builtin_function_t builtin_replace = {.name = L"replace",
                                            .min_args = 2,
                                            .effects = BUILTIN_EFFECT_NONE,
                                            .execute = execute,
                                            .execute_method = execute_method,
                                            .interpret = interpret,
                                            .get_object = get_function_replace};

object_t *get_function_replace(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_replace);
}
