/** @file join.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Waits for an equal program thread without holding the model lock.
 */
#include "analysis/lattice.h"
#include "registry.h"
#include "vm/vm.h"

static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    if (count != 1 || !is_integer_object(args[0]))
        return operation_exception(get_exception_invalid_argument());
    int64_t id = get_object_integer_value(args[0]).value;
    if (id <= 0)
        return operation_exception(get_exception_invalid_argument());
    return join_program_thread(thread, (uint64_t)id);
}

static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    return make_top_element();
}

const builtin_function_t builtin_join = {.name = U"join",
                                         .min_args = 0,
                                         .effects = BUILTIN_EFFECT_BINDINGS,
                                         .execute = execute,
                                         .interpret = interpret,
                                         .get_object = get_function_join};

object_t *get_function_join(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_join);
}
