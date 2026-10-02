/** @file update.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Abstract numeric increment and decrement.
 */
#pragma once
#include "lattice.h"
/** @brief Returns the updated value on normal completion. */
const lattice_element_t *
lattice_update(arena_t *arena, const lattice_element_t *value, bool decrement);
