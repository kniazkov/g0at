/** @file starts_with.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Checks whether a string starts with a given prefix.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "goat/unicode.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "model/object.h"
#include "model/thread.h"
#include "registry.h"

/** @brief Runtime executor for the method form `string.startsWith(str)`. */
static operation_result_t
execute_method(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    (void)thread;
    if (count < 1)
        return operation_exception(get_exception_invalid_argument());
    if (receiver->vtbl->type != TYPE_STRING)
        return operation_exception(get_exception_invalid_argument());
    if (args[0]->vtbl->type != TYPE_STRING && args[0]->vtbl->type != TYPE_CHARACTER)
        return operation_exception(get_exception_invalid_argument());
    string_value_t value = convert_object_to_string(receiver);
    string_value_t prefix = convert_object_to_string(args[0]);
    bool result =
        prefix.length <= value.length && !goat_memcmp(value.data, prefix.data, prefix.length);
    FREE_STRING(value);
    FREE_STRING(prefix);
    return operation_success(get_boolean_object(result));
}

/** @brief Runtime executor for the plain call form `startsWith(string, str)`. */
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
    if (args[0]->type == LATTICE_STRING_CONSTANT) {
        const string_constant_element_t *str = (const string_constant_element_t *)args[0];
        string_view_t prefix;
        if (get_string_view_from_constant(state->arena, args[1], &prefix)) {
            bool result = prefix.length <= str->value.length
                          && !goat_memcmp(str->value.data, prefix.data, prefix.length);
            return result ? make_true_element() : make_false_element();
        }
    }
    return make_boolean_element();
}

const builtin_function_t builtin_starts_with = {.name = U"startsWith",
                                                .min_args = 1,
                                                .effects = BUILTIN_EFFECT_NONE,
                                                .execute = execute,
                                                .execute_method = execute_method,
                                                .interpret = interpret,
                                                .get_object = get_function_starts_with};

object_t *get_function_starts_with(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_starts_with);
}
