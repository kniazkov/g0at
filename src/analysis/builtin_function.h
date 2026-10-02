/** @file builtin_function.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Abstract executors registered by native function descriptors.
 */
#pragma once
#include "abstract_state.h"
#include "lattice.h"

const lattice_element_t *
interpret_builtin_atan(abstract_state_t *state, const lattice_element_t *const *args, size_t count);
const lattice_element_t *interpret_builtin_print(abstract_state_t *state,
                                                 const lattice_element_t *const *args,
                                                 size_t count);
const lattice_element_t *
interpret_builtin_sign(abstract_state_t *state, const lattice_element_t *const *args, size_t count);
const lattice_element_t *
interpret_builtin_sqrt(abstract_state_t *state, const lattice_element_t *const *args, size_t count);
