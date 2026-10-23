/** @file clone.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Object.clone delegates copying to the receiver's virtual table.
 */
#include "analysis/lattice.h"
#include "model/builtin_function.h"
#include "model/context.h"
#include "model/thread.h"

/** @brief Clones the receiver; the virtual method returns an owned reference. */
static operation_result_t
execute_method(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    return operation_success(
        clone_object_with_access(thread->process, receiver, &thread->context->access));
}

/** @brief A detached method accepts the object as its first explicit argument. */
static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    if (!count)
        return operation_exception(get_exception_invalid_argument());
    return operation_success(
        clone_object_with_access(thread->process, args[0], &thread->context->access));
}

/** @brief Copying is type-specific and must not be constant-folded into an alias. */
static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    return count ? make_top_element() : make_bottom_element();
}

/** @brief Method descriptor; exposed through Object rather than the global registry. */
static const builtin_function_t builtin_clone = {.name = U"clone",
                                                 .min_args = 0,
                                                 .effects = BUILTIN_EFFECT_NONE,
                                                 .execute = execute,
                                                 .execute_method = execute_method,
                                                 .interpret = interpret,
                                                 .get_object = get_function_clone};

object_t *get_function_clone(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_clone);
}
