/** @file unary_operation.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Abstract numeric sign operations.
 */
#pragma once
#include "lattice.h"
/** @brief Computes normal results of a unary sign; negate selects minus. */
const lattice_element_t *lattice_unary(arena_t *arena, const lattice_element_t *value, bool negate);
