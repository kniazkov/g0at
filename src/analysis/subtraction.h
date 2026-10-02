/** @file subtraction.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Abstract subtraction shared by interpretation and reachability proofs.
 */
#pragma once
#include "lattice.h"
/** @brief Returns possible normal results; BOTTOM means no successful evaluation. */
const lattice_element_t *
lattice_subtract(arena_t *arena, const lattice_element_t *left, const lattice_element_t *right);
