/** @file last_index_of.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Returns the index of the last occurrence of a substring in a string.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "model/object.h"
#include "model/thread.h"
#include "registry.h"

#include <wchar.h>

/** @brief Searches for the last occurrence of a substring; returns -1 if absent. */
static int64_t find_substring_last_index(const wchar_t *haystack,
                                         size_t haystack_len,
                                         const wchar_t *needle,
                                         size_t needle_len) {
    if (needle_len == 0)
        return (int64_t)haystack_len;
    if (needle_len > haystack_len)
        return -1;
    size_t i = haystack_len - needle_len;
    while (true) {
        if (!wmemcmp(haystack + i, needle, needle_len))
            return (int64_t)i;
        if (i == 0)
            break;
        i--;
    }
    return -1;
}

/** @brief Runtime executor for the method form `string.lastIndexOf(str)`. */
static operation_result_t
execute_method(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    if (count < 1)
        return operation_exception(get_exception_invalid_argument());
    if (receiver->vtbl->type != TYPE_STRING || args[0]->vtbl->type != TYPE_STRING)
        return operation_exception(get_exception_invalid_argument());
    string_value_t value = convert_object_to_string(receiver);
    string_value_t substring = convert_object_to_string(args[0]);
    int64_t result =
        find_substring_last_index(value.data, value.length, substring.data, substring.length);
    FREE_STRING(value);
    FREE_STRING(substring);
    return operation_success(create_integer_object(thread->process, result));
}

/** @brief Runtime executor for the plain call form `lastIndexOf(string, str)`. */
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
    if (args[0]->type == LATTICE_BOTTOM || args[1]->type == LATTICE_BOTTOM)
        return make_bottom_element();
    if (args[0]->type == LATTICE_STRING_CONSTANT && args[1]->type == LATTICE_STRING_CONSTANT) {
        const string_constant_element_t *str = (const string_constant_element_t *)args[0];
        const string_constant_element_t *sub = (const string_constant_element_t *)args[1];
        int64_t result = find_substring_last_index(str->value.data,
                                                   str->value.length,
                                                   sub->value.data,
                                                   sub->value.length);
        return make_integer_constant_element(state->arena, result);
    }
    return make_integer_element();
}

const builtin_function_t builtin_last_index_of = {.name = L"lastIndexOf",
                                                  .min_args = 1,
                                                  .effects = BUILTIN_EFFECT_NONE,
                                                  .execute = execute,
                                                  .execute_method = execute_method,
                                                  .interpret = interpret,
                                                  .get_object = get_function_last_index_of};

object_t *get_function_last_index_of(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_last_index_of);
}
