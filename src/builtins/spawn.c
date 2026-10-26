/** @file spawn.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Starts an equal program thread through the VM lifecycle owner.
 */
#include "analysis/lattice.h"
#include "registry.h"
#include "vm/vm.h"

static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    return spawn_program_thread(thread, args[0], args + 1, count - 1);
}

static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    return make_integer_element();
}

const builtin_function_t builtin_spawn = {.name = U"spawn",
                                          .min_args = 1,
                                          .effects = BUILTIN_EFFECT_BINDINGS,
                                          .execute = execute,
                                          .interpret = interpret,
                                          .get_object = get_function_spawn};

object_t *get_function_spawn(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_spawn);
}
