/** @file bitwise.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Normal results of integer bit operations.
 */
#include "bitwise.h"

const lattice_element_t *lattice_boolean(const lattice_element_t *value, bool negate) {
    abstract_truth_t truth = lattice_truth(value);
    if (truth == ABSTRACT_NEVER)
        return make_bottom_element();
    if (truth == ABSTRACT_EITHER)
        return make_boolean_element();
    return ((truth == ABSTRACT_TRUE) != negate) ? make_true_element() : make_false_element();
}

static bool may_be_integer(const lattice_element_t *value) {
    return is_integer_lattice_element(value) || value->type == LATTICE_NUMERIC
           || value->type == LATTICE_TOP || value->type == LATTICE_NOT_NULL;
}

static bool constant(const lattice_element_t *value, int64_t integer) {
    return value->type == LATTICE_INTEGER_CONSTANT
           && ((const integer_constant_element_t *)value)->value == integer;
}

static const lattice_element_t *integer_part(const lattice_element_t *value) {
    return is_integer_lattice_element(value) ? value : make_integer_element();
}

const lattice_element_t *lattice_bitwise_not(arena_t *arena, const lattice_element_t *value) {
    if (!may_be_integer(value))
        return make_bottom_element();
    if (value->type == LATTICE_INTEGER_CONSTANT)
        return make_integer_constant_element(
            arena,
            invert_integer(((const integer_constant_element_t *)value)->value));
    if (value->type == LATTICE_INTEGER_RANGE) {
        const integer_range_element_t *range = (const integer_range_element_t *)value;
        return make_integer_range_element(arena,
                                          invert_integer(range->max),
                                          invert_integer(range->min));
    }
    return make_integer_element();
}

const lattice_element_t *lattice_bitwise(arena_t *arena,
                                         const lattice_element_t *left,
                                         const lattice_element_t *right,
                                         bitwise_kind_t kind) {
    if (!may_be_integer(left) || !may_be_integer(right))
        return make_bottom_element();
    bool shift = kind == BIT_SHIFT_LEFT || kind == BIT_SHIFT_RIGHT;
    if (shift) {
        if (right->type == LATTICE_INTEGER_CONSTANT) {
            int64_t n = ((const integer_constant_element_t *)right)->value;
            if (n < 0 || n > 63)
                return make_bottom_element();
        } else if (right->type == LATTICE_INTEGER_RANGE) {
            const integer_range_element_t *r = (const integer_range_element_t *)right;
            if (r->max < 0 || r->min > 63)
                return make_bottom_element();
        }
    }
    if (left->type == LATTICE_INTEGER_CONSTANT && right->type == LATTICE_INTEGER_CONSTANT)
        return make_integer_constant_element(
            arena,
            bitwise_integer(((const integer_constant_element_t *)left)->value,
                            ((const integer_constant_element_t *)right)->value,
                            kind));
    if (kind == BIT_AND) {
        if (constant(left, 0) || constant(right, 0))
            return make_integer_constant_element(arena, 0);
        if (constant(left, -1))
            return integer_part(right);
        if (constant(right, -1))
            return integer_part(left);
        const lattice_element_t *mask = left->type == LATTICE_INTEGER_CONSTANT ? left : right;
        if (mask->type == LATTICE_INTEGER_CONSTANT) {
            int64_t n = ((const integer_constant_element_t *)mask)->value;
            if (n >= 0)
                return make_integer_range_element(arena, 0, n);
        }
    }
    if (kind == BIT_OR && (constant(left, -1) || constant(right, -1)))
        return make_integer_constant_element(arena, -1);
    if (kind == BIT_OR || kind == BIT_XOR) {
        if (constant(left, 0))
            return integer_part(right);
        if (constant(right, 0))
            return integer_part(left);
    }
    if (kind == BIT_XOR) {
        if (constant(left, -1))
            return lattice_bitwise_not(arena, right);
        if (constant(right, -1))
            return lattice_bitwise_not(arena, left);
    }
    if (shift) {
        if (constant(left, 0))
            return make_integer_constant_element(arena, 0);
        if (constant(right, 0))
            return integer_part(left);
        if (kind == BIT_SHIFT_RIGHT && constant(left, -1))
            return make_integer_constant_element(arena, -1);
    }
    return make_integer_element();
}
