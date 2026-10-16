/** @file substring.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Returns a substring of a string.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "model/object.h"
#include "model/thread.h"
#include "registry.h"

#include <wchar.h>

/** @brief Runtime executor for the method form `string.substring(begin, end)`. */
static operation_result_t
execute_method(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    if (count < 1)
        return operation_exception(get_exception_invalid_argument());
    if (receiver->vtbl->type != TYPE_STRING)
        return operation_exception(get_exception_invalid_argument());
    if (!is_integer_object(args[0]))
        return operation_exception(get_exception_invalid_argument());
    string_value_t value = convert_object_to_string(receiver);
    int64_t begin = get_object_integer_value(args[0]).value;
    int64_t end = (int64_t)value.length;
    if (count >= 2) {
        if (!is_integer_object(args[1])) {
            FREE_STRING(value);
            return operation_exception(get_exception_invalid_argument());
        }
        end = get_object_integer_value(args[1]).value;
    }
    if (begin < 0 || end < begin || (uint64_t)end > value.length) {
        FREE_STRING(value);
        return operation_exception(get_exception_invalid_argument());
    }
    size_t start = (size_t)begin;
    size_t stop = (size_t)end;
    wchar_t *copy = ALLOC((stop - start + 1) * sizeof(wchar_t));
    wmemcpy(copy, value.data + start, stop - start);
    copy[stop - start] = 0;
    FREE_STRING(value);
    return operation_success(
        create_string_object(thread->process, (string_value_t){copy, stop - start, true}));
}

/** @brief Runtime executor for the plain call form `substring(string, begin, end)`. */
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
    if (receiver->type == LATTICE_STRING_CONSTANT && args[1]->type == LATTICE_INTEGER_CONSTANT
        && (count < 3 || args[2]->type == LATTICE_INTEGER_CONSTANT)) {
        const string_constant_element_t *str = (const string_constant_element_t *)receiver;
        int64_t begin = ((const integer_constant_element_t *)args[1])->value;
        int64_t end = (int64_t)str->value.length;
        if (count >= 3)
            end = ((const integer_constant_element_t *)args[2])->value;
        if (begin >= 0 && end >= begin && (uint64_t)end <= str->value.length) {
            size_t start = (size_t)begin;
            size_t stop = (size_t)end;
            return make_string_constant_element(
                state->arena,
                copy_string_to_arena(state->arena, str->value.data + start, stop - start));
        }
        return make_bottom_element();
    }
    return make_string_element();
}

const builtin_function_t builtin_substring = {.name = L"substring",
                                              .min_args = 1,
                                              .effects = BUILTIN_EFFECT_NONE,
                                              .execute = execute,
                                              .execute_method = execute_method,
                                              .interpret = interpret,
                                              .get_object = get_function_substring};

object_t *get_function_substring(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_substring);
}
