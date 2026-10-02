/** @file modulo.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Normal-result domains for integer remainder.
 */
#include "modulo.h"

static uint64_t magnitude(int64_t value) {
    return value < 0 ? 0 - (uint64_t)value : (uint64_t)value;
}

/** @brief Bounds the integer members of a domain. */
static void bounds(const lattice_element_t *value, int64_t *min, int64_t *max) {
    if (value->type == LATTICE_INTEGER_CONSTANT) {
        *min = *max = ((const integer_constant_element_t *)value)->value;
    } else if (value->type == LATTICE_INTEGER_RANGE) {
        *min = ((const integer_range_element_t *)value)->min;
        *max = ((const integer_range_element_t *)value)->max;
    } else {
        *min = INT64_MIN;
        *max = INT64_MAX;
    }
}

const lattice_element_t *
lattice_modulo(arena_t *arena, const lattice_element_t *left, const lattice_element_t *right) {
    if (left->type == LATTICE_BOTTOM || right->type == LATTICE_BOTTOM)
        return make_bottom_element();
    if (left->type == LATTICE_TOP || left->type == LATTICE_NOT_NULL || left->type == LATTICE_ARRAY
        || left->type == LATTICE_TYPED_ARRAY)
        return make_top_element();
    if (!is_integer_lattice_element(left) && left->type != LATTICE_NUMERIC)
        return make_bottom_element();
    if (!is_integer_lattice_element(right) && right->type != LATTICE_NUMERIC
        && right->type != LATTICE_TOP && right->type != LATTICE_NOT_NULL)
        return make_bottom_element();
    int64_t amin, amax, bmin, bmax;
    bounds(left, &amin, &amax);
    bounds(right, &bmin, &bmax);
    if (bmin == 0 && bmax == 0)
        return make_bottom_element();
    if (amin == amax && bmin == bmax) {
        int64_t result = amin == INT64_MIN && bmin == -1 ? 0 : amin % bmin;
        return make_integer_constant_element(arena, result);
    }
    uint64_t magnitude_min = magnitude(bmin), magnitude_max = magnitude(bmax);
    uint64_t largest = magnitude_min > magnitude_max ? magnitude_min : magnitude_max;
    /* |remainder| < |divisor|, even when the divisor is INT64_MIN. */
    int64_t limit = (int64_t)(largest - 1);
    int64_t min = amin < 0 ? (amin > -limit ? amin : -limit) : 0;
    int64_t max = amax > 0 ? (amax < limit ? amax : limit) : 0;
    return make_integer_range_element(arena, min, max);
}
