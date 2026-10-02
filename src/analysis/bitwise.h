/** @file bitwise.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Abstract integer bit operations and truth conversion.
 */
#pragma once
#include "lattice.h"
#include "lib/bitwise.h"
const lattice_element_t *lattice_boolean(const lattice_element_t *value, bool negate);
const lattice_element_t *lattice_bitwise_not(arena_t *arena, const lattice_element_t *value);
const lattice_element_t *lattice_bitwise(arena_t *arena,
                                         const lattice_element_t *left,
                                         const lattice_element_t *right,
                                         bitwise_kind_t kind);
