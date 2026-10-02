/** @file function_call.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Abstract invocation of known function bodies.
 */
#pragma once

#include "abstract_state.h"
#include "lattice.h"

/** @brief Evaluates a known callable; unknown calls invalidate captured facts. */
const lattice_element_t *interpret_function_call(const lattice_element_t *function,
                                                 const lattice_element_t *const *args,
                                                 size_t count,
                                                 abstract_state_t *caller);

/** @brief Saves the state of a normal return before its path terminates. */
void collect_abstract_return(abstract_state_t *state);
