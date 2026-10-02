/** @file division.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Normal-result domains for numeric division.
 */
#include "division.h"

#include "lib/integer_math.h"
#include "unary_operation.h"

static bool is_constant(const lattice_element_t *value) {
    return value->type == LATTICE_INTEGER_CONSTANT || value->type == LATTICE_REAL_CONSTANT;
}

static double constant_real(const lattice_element_t *value) {
    return value->type == LATTICE_INTEGER_CONSTANT
               ? integer_to_double(((const integer_constant_element_t *)value)->value)
               : ((const real_constant_element_t *)value)->value;
}

const lattice_element_t *
lattice_divide(arena_t *arena, const lattice_element_t *left, const lattice_element_t *right) {
    if (left->type == LATTICE_BOTTOM || right->type == LATTICE_BOTTOM)
        return make_bottom_element();
    if (left->type == LATTICE_TOP || left->type == LATTICE_NOT_NULL || left->type == LATTICE_ARRAY
        || left->type == LATTICE_TYPED_ARRAY)
        return make_top_element();
    if (!is_numeric_lattice_element(left))
        return make_bottom_element();
    if (right->type == LATTICE_TOP || right->type == LATTICE_NOT_NULL)
        return is_real_lattice_element(left) ? make_real_element() : make_numeric_element();
    if (!is_numeric_lattice_element(right))
        return make_bottom_element();
    if (is_constant(right) && constant_real(right) == 0)
        return make_bottom_element();
    if (left->type == LATTICE_INTEGER_CONSTANT && right->type == LATTICE_INTEGER_CONSTANT) {
        int64_t a = ((const integer_constant_element_t *)left)->value;
        int64_t b = ((const integer_constant_element_t *)right)->value;
        if (a == INT64_MIN && b == -1)
            return make_real_constant_element(arena, 0x1p63);
        if (a % b == 0)
            return make_integer_constant_element(arena, a / b);
    }
    if (is_constant(left) && is_constant(right))
        return make_real_constant_element(arena, constant_real(left) / constant_real(right));
    if (is_real_lattice_element(left) || is_real_lattice_element(right))
        return make_real_element();
    if (is_integer_lattice_element(left) && is_integer_lattice_element(right)) {
        if (left->type == LATTICE_INTEGER_CONSTANT
            && ((const integer_constant_element_t *)left)->value == 0)
            return left;
        if (right->type == LATTICE_INTEGER_CONSTANT) {
            int64_t divisor = ((const integer_constant_element_t *)right)->value;
            if (divisor == 1)
                return left;
            if (divisor == -1 && left->type == LATTICE_INTEGER_RANGE
                && ((const integer_range_element_t *)left)->min > INT64_MIN)
                return lattice_unary(arena, left, true);
        }
    }
    /* Integer intervals can yield both exact integer and fractional real quotients. */
    return make_numeric_element();
}
