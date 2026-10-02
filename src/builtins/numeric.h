/** @file numeric.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared numeric-domain helpers for native abstract executors.
 */
#pragma once
#include "analysis/lattice.h"
#include "lib/integer_math.h"

/** @brief Extracts a numeric constant using the runtime's integer rounding. */
static inline bool builtin_numeric_constant(const lattice_element_t *value, double *number) {
    if (value->type == LATTICE_INTEGER_CONSTANT) {
        *number = integer_to_double(((const integer_constant_element_t *)value)->value);
        return true;
    }
    if (value->type == LATTICE_REAL_CONSTANT) {
        *number = ((const real_constant_element_t *)value)->value;
        return true;
    }
    return false;
}

/** @brief Whether the runtime type may be numeric or nonnumeric. */
static inline bool builtin_unknown_type(const lattice_element_t *value) {
    return value->type == LATTICE_TOP || value->type == LATTICE_NOT_NULL;
}
