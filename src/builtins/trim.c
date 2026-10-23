/** @file trim.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Removes leading and trailing whitespace from a string.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "goat/unicode.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "model/object.h"
#include "model/thread.h"
#include "registry.h"

/** @brief Trims whitespace from both ends of a string object. */
static operation_result_t trim_string(thread_t *thread, object_t *str) {
    if (str->vtbl->type != TYPE_STRING)
        return operation_exception(get_exception_invalid_operation());
    string_value_t value = convert_object_to_string(str);
    size_t start = 0;
    size_t end = value.length;
    while (start < end && goat_is_space(value.data[start]))
        start++;
    while (end > start && goat_is_space(value.data[end - 1]))
        end--;
    if (start == 0 && end == value.length) {
        FREE_STRING(value);
        INCREF(str);
        return operation_success(str);
    }
    goat_char_t *copy = ALLOC((end - start + 1) * sizeof(goat_char_t));
    goat_memcpy(copy, value.data + start, end - start);
    copy[end - start] = 0;
    FREE_STRING(value);
    return operation_success(
        create_string_object(thread->process, (string_value_t){copy, end - start, true}));
}

/** @brief Runtime executor for the plain call form `trim(string)`. */
static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    if (count < 1)
        return operation_exception(get_exception_invalid_argument());
    return trim_string(thread, args[0]);
}

/** @brief Runtime executor for the method form `string.trim()`. */
static operation_result_t
execute_method(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    (void)count;
    return trim_string(thread, receiver);
}

/** @brief Normal-result domain; does not execute runtime effects. */
static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    if (count < 1)
        return make_bottom_element();
    const lattice_element_t *arg = args[0];
    if (arg->type == LATTICE_STRING_CONSTANT) {
        const string_constant_element_t *constant = (const string_constant_element_t *)arg;
        string_view_t view = constant->value;
        size_t start = 0;
        size_t end = view.length;
        while (start < end && goat_is_space(view.data[start]))
            start++;
        while (end > start && goat_is_space(view.data[end - 1]))
            end--;
        return make_string_constant_element(
            state->arena,
            copy_string_to_arena(state->arena, view.data + start, end - start));
    }
    if (arg->type == LATTICE_STRING)
        return make_string_element();
    if (arg->type == LATTICE_BOTTOM)
        return arg;
    return make_string_element();
}

const builtin_function_t builtin_trim = {.name = U"trim",
                                         .min_args = 0,
                                         .effects = BUILTIN_EFFECT_NONE,
                                         .execute = execute,
                                         .execute_method = execute_method,
                                         .interpret = interpret,
                                         .get_object = get_function_trim};

object_t *get_function_trim(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_trim);
}
