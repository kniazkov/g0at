/** @file is_empty.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Checks whether a string is empty.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "goat/unicode.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "model/object.h"
#include "model/thread.h"
#include "registry.h"

/** @brief Runtime executor for the method form `string.isEmpty()`. */
static operation_result_t
execute_method(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    (void)count;
    (void)thread;
    if (receiver->vtbl->type != TYPE_STRING)
        return operation_exception(get_exception_invalid_argument());
    string_value_t value = convert_object_to_string(receiver);
    bool result = value.length == 0;
    FREE_STRING(value);
    return operation_success(get_boolean_object(result));
}

/** @brief Runtime executor for the plain call form `isEmpty(string)`. */
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
    if (args[0]->type == LATTICE_STRING_CONSTANT) {
        const string_constant_element_t *str = (const string_constant_element_t *)args[0];
        return str->value.length == 0 ? make_true_element() : make_false_element();
    }
    return make_boolean_element();
}

const builtin_function_t builtin_is_empty = {.name = U"isEmpty",
                                             .min_args = 0,
                                             .effects = BUILTIN_EFFECT_NONE,
                                             .execute = execute,
                                             .execute_method = execute_method,
                                             .interpret = interpret,
                                             .get_object = get_function_is_empty};

object_t *get_function_is_empty(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_is_empty);
}
