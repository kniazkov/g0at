/** @file input.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Blocking line input; analysis never reads stdin.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "lib/io.h"
#include "model/thread.h"
#include "registry.h"

#include <stdio.h>

static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    fflush(stdout);
    string_value_t line = read_input_line(stdin);
    if (!line.data)
        return operation_exception(get_exception_invalid_operation());
    return operation_success(create_string_object(thread->process, line));
}

static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    return make_string_element();
}

const builtin_function_t builtin_input = {.name = L"input",
                                          .min_args = 0,
                                          .effects = BUILTIN_EFFECT_INPUT,
                                          .execute = execute,
                                          .interpret = interpret,
                                          .get_object = get_function_input};

object_t *get_function_input(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_input);
}
