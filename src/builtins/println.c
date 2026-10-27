/** @file println.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Runtime and abstract implementations of println.
 */
#include "lib/files/io.h"
#include "model/process.h"
#include "model/thread.h"
#include "registry.h"

/** @brief Prints the same value as print, followed by one newline. */
static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    operation_result_t result = builtin_print.execute(args, count, thread);
    if (!result.is_exception) {
        size_t depth = process_execution_suspend(thread->process);
        print_utf8(U"\n");
        process_execution_resume(thread->process, depth);
    }
    return result;
}

/** @brief Shares print's normal-result domain without performing output. */
static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    return builtin_print.interpret(state, args, count);
}

const builtin_function_t builtin_println = {.name = U"println",
                                            .min_args = 1,
                                            .effects = BUILTIN_EFFECT_OUTPUT,
                                            .execute = execute,
                                            .interpret = interpret,
                                            .get_object = get_function_println};

object_t *get_function_println(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_println);
}
