/** @file multiplication.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Normal-result domains for numeric multiplication.
 */
#include "multiplication.h"

#include "lib/integer_math.h"

/** @brief Checks magnitudes before multiplying signed endpoints. */
static bool multiply_checked(int64_t left, int64_t right, int64_t *out) {
    uint64_t a = left < 0 ? 0 - (uint64_t)left : (uint64_t)left;
    uint64_t b = right < 0 ? 0 - (uint64_t)right : (uint64_t)right;
    uint64_t limit = (left < 0) != (right < 0) ? (uint64_t)INT64_MAX + 1 : INT64_MAX;
    if (b != 0 && a > limit / b)
        return false;
    *out = multiply_int64_wrapping(left, right);
    return true;
}

static bool is_constant(const lattice_element_t *value) {
    return value->type == LATTICE_INTEGER_CONSTANT || value->type == LATTICE_REAL_CONSTANT;
}

static double constant_real(const lattice_element_t *value) {
    return value->type == LATTICE_INTEGER_CONSTANT
               ? integer_to_double(((const integer_constant_element_t *)value)->value)
               : ((const real_constant_element_t *)value)->value;
}

/** @brief Treats integer constants as singleton intervals. */
static void bounds(const lattice_element_t *value, int64_t *min, int64_t *max) {
    if (value->type == LATTICE_INTEGER_CONSTANT) {
        *min = *max = ((const integer_constant_element_t *)value)->value;
    } else {
        *min = ((const integer_range_element_t *)value)->min;
        *max = ((const integer_range_element_t *)value)->max;
    }
}

const lattice_element_t *
lattice_multiply(arena_t *arena, const lattice_element_t *left, const lattice_element_t *right) {
    if (left->type == LATTICE_BOTTOM || right->type == LATTICE_BOTTOM)
        return make_bottom_element();
    /* Unknown receivers and future array models cannot yet constrain dispatch. */
    if (left->type == LATTICE_TOP || left->type == LATTICE_NOT_NULL || left->type == LATTICE_ARRAY
        || left->type == LATTICE_TYPED_ARRAY)
        return make_top_element();
    if (!is_numeric_lattice_element(left))
        return make_bottom_element();
    if (right->type == LATTICE_TOP || right->type == LATTICE_NOT_NULL)
        return is_real_lattice_element(left) ? make_real_element() : make_numeric_element();
    if (!is_numeric_lattice_element(right))
        return make_bottom_element();
    if (left->type == LATTICE_INTEGER_CONSTANT && right->type == LATTICE_INTEGER_CONSTANT)
        return make_integer_constant_element(
            arena,
            multiply_int64_wrapping(((const integer_constant_element_t *)left)->value,
                                    ((const integer_constant_element_t *)right)->value));
    if (is_constant(left) && is_constant(right))
        return make_real_constant_element(arena, constant_real(left) * constant_real(right));
    if (is_real_lattice_element(left) || is_real_lattice_element(right))
        return make_real_element();
    if (left->type == LATTICE_NUMERIC || right->type == LATTICE_NUMERIC)
        return make_numeric_element();
    if (left->type == LATTICE_INTEGER || right->type == LATTICE_INTEGER)
        return make_integer_element();
    int64_t left_min, left_max, right_min, right_max, products[4];
    bounds(left, &left_min, &left_max);
    bounds(right, &right_min, &right_max);
    if (!multiply_checked(left_min, right_min, &products[0])
        || !multiply_checked(left_min, right_max, &products[1])
        || !multiply_checked(left_max, right_min, &products[2])
        || !multiply_checked(left_max, right_max, &products[3]))
        return make_integer_element();
    int64_t min = products[0], max = products[0];
    for (size_t i = 1; i < 4; i++) {
        if (products[i] < min)
            min = products[i];
        if (products[i] > max)
            max = products[i];
    }
    return make_integer_range_element(arena, min, max);
}
