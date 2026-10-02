/** @file math_function.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared numeric checks and constant folding for libm functions.
 */
#pragma once
#include "analysis/abstract_state.h"
#include "model/builtin_function.h"

typedef double (*unary_math_t)(double);
typedef double (*binary_math_t)(double, double);

operation_result_t execute_unary_math(object_t **args, thread_t *thread, unary_math_t function);
operation_result_t execute_binary_math(object_t **args, thread_t *thread, binary_math_t function);
const lattice_element_t *interpret_unary_math(abstract_state_t *state,
                                              const lattice_element_t *const *args,
                                              unary_math_t function);
const lattice_element_t *interpret_binary_math(abstract_state_t *state,
                                               const lattice_element_t *const *args,
                                               binary_math_t function);
