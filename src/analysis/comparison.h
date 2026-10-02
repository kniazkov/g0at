/** @file comparison.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Abstract comparison results on normal execution paths.
 */
#pragma once
#include "lattice.h"
#include "lib/comparison.h"

const lattice_element_t *lattice_compare(const lattice_element_t *left,
                                         const lattice_element_t *right,
                                         comparison_kind_t kind);
