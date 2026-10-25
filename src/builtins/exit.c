/** @file exit.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Strict portable process termination, separate from exception unwinding.
 */
#include "analysis/lattice.h"
#include "model/process.h"
#include "model/thread.h"
#include "registry.h"

/** @brief Validate before choosing a status; initialization failures remain catchable. */
static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    if (count != 1 || !is_integer_object(args[0]))
        return operation_exception(get_exception_invalid_argument());
    int64_t code = get_object_integer_value(args[0]).value;
    if (code < 0 || code > 255)
        return operation_exception(get_exception_invalid_argument());
    if (thread->initialization_depth)
        return operation_exception(get_exception_exit_during_initialization());
    process_t *process = thread->process;
    if (!process->exit_requested) {
        process->exit_code = (int)code;
        process->exit_requested = true;
    }
    return operation_success(get_null_object());
}

/** @brief No normal return, either termination or an ordinary exception. */
static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    return make_bottom_element();
}

const builtin_function_t builtin_exit = {.name = U"exit",
                                         .min_args = 0,
                                         .effects = BUILTIN_EFFECT_TERMINATION,
                                         .execute = execute,
                                         .interpret = interpret,
                                         .get_object = get_function_exit};

object_t *get_function_exit(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_exit);
}
