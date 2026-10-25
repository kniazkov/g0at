/** @file print.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Runtime and abstract implementations of print.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "lib/allocate.h"
#include "lib/io.h"
#include "model/context.h"
#include "model/thread.h"
#include "registry.h"

/** @brief Runtime executor; the shared caller has checked minimum arity. */
static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    exception_t failure = object_observation_exception(args[0], &thread->context->access);
    if (failure.value) {
        INCREF(failure.value);
        return (operation_result_t){failure.value, true, failure.trace};
    }
    string_value_t str = object_to_string_with_access(args[0], &thread->context->access);
    if (str.data) {
        print_utf8_view(VALUE_TO_VIEW(str));
        FREE_STRING(str);
    }
    return operation_success(get_null_object());
}

/** @brief Normal-result domain; does not execute runtime effects. */
static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    return make_null_element();
}

const builtin_function_t builtin_print = {.name = U"print",
                                          .min_args = 1,
                                          .effects = BUILTIN_EFFECT_OUTPUT,
                                          .execute = execute,
                                          .interpret = interpret,
                                          .get_object = get_function_print};

object_t *get_function_print(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_print);
}
