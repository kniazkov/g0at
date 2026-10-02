/** @file power.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Normal-result domains for real-valued exponentiation.
 */
#include "power.h"

#include "lib/integer_math.h"

#include <math.h>

static bool is_constant(const lattice_element_t *value) {
    return value->type == LATTICE_INTEGER_CONSTANT || value->type == LATTICE_REAL_CONSTANT;
}

static double constant_real(const lattice_element_t *value) {
    return value->type == LATTICE_INTEGER_CONSTANT
               ? integer_to_double(((const integer_constant_element_t *)value)->value)
               : ((const real_constant_element_t *)value)->value;
}

const lattice_element_t *
lattice_power(arena_t *arena, const lattice_element_t *left, const lattice_element_t *right) {
    if (left->type == LATTICE_BOTTOM || right->type == LATTICE_BOTTOM)
        return make_bottom_element();
    if (left->type == LATTICE_TOP || left->type == LATTICE_NOT_NULL || left->type == LATTICE_ARRAY
        || left->type == LATTICE_TYPED_ARRAY)
        return make_top_element();
    if (!is_numeric_lattice_element(left))
        return make_bottom_element();
    if (right->type == LATTICE_TOP || right->type == LATTICE_NOT_NULL)
        return make_real_element();
    if (!is_numeric_lattice_element(right))
        return make_bottom_element();
    if (is_constant(left) && is_constant(right))
        return make_real_constant_element(arena, pow(constant_real(left), constant_real(right)));
    /* pow(NaN, 0) and pow(1, NaN) are both 1; operands are already evaluated. */
    if ((is_constant(right) && constant_real(right) == 0)
        || (is_constant(left) && constant_real(left) == 1))
        return make_real_constant_element(arena, 1);
    return make_real_element();
}
