/** @file unary_operation.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Abstract numeric sign operations.
 */
#include "unary_operation.h"

#include "lib/integer_math.h"

const lattice_element_t *
lattice_unary(arena_t *arena, const lattice_element_t *value, bool negate) {
    if (value->type == LATTICE_BOTTOM)
        return value;
    if (value->type == LATTICE_TOP || value->type == LATTICE_NOT_NULL
        || value->type == LATTICE_ARRAY || value->type == LATTICE_TYPED_ARRAY)
        return make_top_element();
    if (!is_numeric_lattice_element(value))
        return make_bottom_element();
    if (!negate)
        return value;
    if (value->type == LATTICE_INTEGER_CONSTANT)
        return make_integer_constant_element(
            arena,
            subtract_int64_wrapping(0, ((const integer_constant_element_t *)value)->value));
    if (value->type == LATTICE_REAL_CONSTANT)
        return make_real_constant_element(arena, -((const real_constant_element_t *)value)->value);
    if (value->type == LATTICE_INTEGER_RANGE) {
        const integer_range_element_t *range = (const integer_range_element_t *)value;
        if (range->min == INT64_MIN)
            return make_integer_element();
        return make_integer_range_element(arena, -range->max, -range->min);
    }
    return value;
}
