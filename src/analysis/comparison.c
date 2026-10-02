/** @file comparison.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Constant folding and interval proofs for comparisons.
 */
#include "comparison.h"

#include <math.h>

/** @brief Groups compatible domains; zero means the runtime type is unknown. */
static int group(lattice_type_t type) {
    if (is_numeric_lattice_type(type))
        return 1;
    switch (type) {
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
            return 2;
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
            return 3;
        case LATTICE_NULL:
            return 4;
        case LATTICE_FUNCTION:
            return 5;
        case LATTICE_USER_DEFINED_OBJECT:
            return 6;
        default:
            return 0;
    }
}

static const lattice_element_t *result(bool value) {
    return value ? make_true_element() : make_false_element();
}

/** @brief An exact integer or real endpoint; integers never pass through double. */
typedef struct {
    bool real;
    int64_t integer;
    double number;
} endpoint_t;

static bool bounds(const lattice_element_t *value, endpoint_t *min, endpoint_t *max) {
    *min = (endpoint_t){0};
    *max = (endpoint_t){0};
    switch (value->type) {
        case LATTICE_INTEGER:
            min->integer = INT64_MIN;
            max->integer = INT64_MAX;
            return true;
        case LATTICE_INTEGER_RANGE:
            min->integer = ((const integer_range_element_t *)value)->min;
            max->integer = ((const integer_range_element_t *)value)->max;
            return true;
        case LATTICE_INTEGER_CONSTANT:
            min->integer = max->integer = ((const integer_constant_element_t *)value)->value;
            return true;
        case LATTICE_REAL_CONSTANT:
            min->real = max->real = true;
            min->number = max->number = ((const real_constant_element_t *)value)->value;
            return true;
        case LATTICE_BOOLEAN:
            max->integer = 1;
            return true;
        case LATTICE_TRUE:
            min->integer = max->integer = 1;
            return true;
        case LATTICE_FALSE:
            return true;
        default:
            return false;
    }
}

static comparison_order_t order(endpoint_t a, endpoint_t b) {
    if (!a.real && !b.real)
        return a.integer < b.integer   ? ORDER_LESS
               : a.integer > b.integer ? ORDER_GREATER
                                       : ORDER_EQUAL;
    if (a.real && b.real)
        return compare_reals(a.number, b.number);
    if (!a.real)
        return compare_integer_real(a.integer, b.number);
    comparison_order_t reverse = compare_integer_real(b.integer, a.number);
    return reverse == ORDER_LESS ? ORDER_GREATER : reverse == ORDER_GREATER ? ORDER_LESS : reverse;
}

static bool nan_constant(const lattice_element_t *value) {
    return value->type == LATTICE_REAL_CONSTANT
           && isnan(((const real_constant_element_t *)value)->value);
}

const lattice_element_t *lattice_compare(const lattice_element_t *left,
                                         const lattice_element_t *right,
                                         comparison_kind_t kind) {
    if (left->type == LATTICE_BOTTOM || right->type == LATTICE_BOTTOM)
        return make_bottom_element();
    bool equality = kind == COMPARE_EQUAL || kind == COMPARE_NOT_EQUAL;
    int a = group(left->type), b = group(right->type);
    if (!equality && ((a && a > 3) || (b && b > 3) || (a && b && a != b)))
        return make_bottom_element();
    if (equality
        && ((a && b && a != b) || (left->type == LATTICE_NULL && right->type == LATTICE_NOT_NULL)
            || (right->type == LATTICE_NULL && left->type == LATTICE_NOT_NULL)))
        return result(kind == COMPARE_NOT_EQUAL);
    if (!a || !b)
        return make_boolean_element();
    if (a == 4 && b == 4)
        return result(kind == COMPARE_EQUAL);
    if (a > 3 || b > 3)
        return make_boolean_element();
    if (a == 1 && (nan_constant(left) || nan_constant(right)))
        return result(comparison_matches(ORDER_UNORDERED, kind));
    if (left->type == LATTICE_STRING_CONSTANT && right->type == LATTICE_STRING_CONSTANT)
        return result(
            comparison_matches(compare_strings(((const string_constant_element_t *)left)->value,
                                               ((const string_constant_element_t *)right)->value),
                               kind));
    endpoint_t lo1, hi1, lo2, hi2;
    if (!bounds(left, &lo1, &hi1) || !bounds(right, &lo2, &hi2))
        return make_boolean_element();
    unsigned possible = 0;
    if (order(lo1, hi2) == ORDER_LESS)
        possible |= ORDER_LESS;
    if (order(hi1, lo2) == ORDER_GREATER)
        possible |= ORDER_GREATER;
    if (order(lo1, hi2) != ORDER_GREATER && order(hi1, lo2) != ORDER_LESS)
        possible |= ORDER_EQUAL;
    bool yes = false, no = false;
    for (unsigned bit = ORDER_LESS; bit <= ORDER_UNORDERED; bit <<= 1) {
        if (possible & bit) {
            if (comparison_matches((comparison_order_t)bit, kind))
                yes = true;
            else
                no = true;
        }
    }
    return yes && no ? make_boolean_element() : result(yes);
}
