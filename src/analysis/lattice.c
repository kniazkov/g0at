/**
 * @file lattice.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Join and meet operations for abstract-interpretation lattice elements.
 *
 * This file implements the two fundamental lattice operations:
 * - join: computes the least upper bound of two abstract values;
 * - meet: computes the greatest lower bound of two abstract values.
 *
 * In less ceremonial terms, join merges facts coming from different execution
 * paths, while meet narrows facts when additional constraints are known. This
 * is where uncertainty is either politely combined or beaten into a smaller
 * shape.
 */

#include "lattice.h"

/**
 * @brief Creates a bottom lattice element.
 *
 * @return Bottom element.
 */
static lattice_element_t make_bottom() {
    return (lattice_element_t){
        .type = LATTICE_BOTTOM,
        .is_constant = false,
        .can_be_null = false
    };
}

/**
 * @brief Creates a top lattice element.
 *
 * @param can_be_null Whether null is included as a possible value.
 * @return Top element.
 */
static lattice_element_t make_top(bool can_be_null) {
    return (lattice_element_t){
        .type = LATTICE_TOP,
        .is_constant = false,
        .can_be_null = can_be_null
    };
}

/**
 * @brief Creates a numeric lattice element.
 *
 * @param can_be_null Whether null is included as a possible value.
 * @return Numeric element.
 */
static lattice_element_t make_numeric(bool can_be_null) {
    return (lattice_element_t){
        .type = LATTICE_NUMERIC,
        .is_constant = false,
        .can_be_null = can_be_null
    };
}

/**
 * @brief Creates a general integer lattice element.
 *
 * @param can_be_null Whether null is included as a possible value.
 * @return Integer element.
 */
static lattice_element_t make_integer(bool can_be_null) {
    return (lattice_element_t){
        .type = LATTICE_INTEGER,
        .is_constant = false,
        .can_be_null = can_be_null
    };
}

/**
 * @brief Creates an exact integer constant lattice element.
 *
 * @param value Exact integer value.
 * @param can_be_null Whether null is included as a possible value.
 * @return Integer constant element.
 */
static lattice_element_t make_integer_constant(int64_t value, bool can_be_null) {
    return (lattice_element_t){
        .type = LATTICE_INTEGER,
        .value.int_value = value,
        .is_constant = true,
        .can_be_null = can_be_null
    };
}

/**
 * @brief Creates an integer interval lattice element.
 *
 * A one-point interval is normalized to an integer constant, because carrying
 * [x, x] around instead of x is the kind of thing that makes analyzers smell
 * like committee decisions.
 *
 * @param min Inclusive lower bound.
 * @param max Inclusive upper bound.
 * @param can_be_null Whether null is included as a possible value.
 * @return Integer interval, integer constant, or bottom if the interval is invalid.
 */
static lattice_element_t make_integer_interval(int64_t min, int64_t max, bool can_be_null) {
    if (min > max) {
        return make_bottom();
    }
    if (min == max) {
        return make_integer_constant(min, can_be_null);
    }
    return (lattice_element_t){
        .type = LATTICE_INTEGER_INTERVAL,
        .value.int_interval = { .min = min, .max = max },
        .is_constant = false,
        .can_be_null = can_be_null
    };
}

/**
 * @brief Creates a general real lattice element.
 *
 * @param can_be_null Whether null is included as a possible value.
 * @return Real element.
 */
static lattice_element_t make_real(bool can_be_null) {
    return (lattice_element_t){
        .type = LATTICE_REAL,
        .is_constant = false,
        .can_be_null = can_be_null
    };
}

/**
 * @brief Creates an exact real constant lattice element.
 *
 * @param value Exact real value.
 * @param can_be_null Whether null is included as a possible value.
 * @return Real constant element.
 */
static lattice_element_t make_real_constant(double value, bool can_be_null) {
    return (lattice_element_t){
        .type = LATTICE_REAL,
        .value.real_value = value,
        .is_constant = true,
        .can_be_null = can_be_null
    };
}

/**
 * @brief Creates a general boolean lattice element.
 *
 * @param can_be_null Whether null is included as a possible value.
 * @return Boolean element.
 */
static lattice_element_t make_boolean(bool can_be_null) {
    return (lattice_element_t){
        .type = LATTICE_BOOLEAN,
        .is_constant = false,
        .can_be_null = can_be_null
    };
}

/**
 * @brief Creates an exact boolean constant lattice element.
 *
 * @param value Exact boolean value.
 * @param can_be_null Whether null is included as a possible value.
 * @return Boolean constant element.
 */
static lattice_element_t make_boolean_constant(bool value, bool can_be_null) {
    return (lattice_element_t){
        .type = LATTICE_BOOLEAN,
        .value.bool_value = value,
        .is_constant = true,
        .can_be_null = can_be_null
    };
}

/**
 * @brief Creates a null lattice element.
 *
 * @return Null element.
 */
static lattice_element_t make_null() {
    return (lattice_element_t){
        .type = LATTICE_NULL,
        .is_constant = false,
        .can_be_null = true
    };
}

/**
 * @brief Checks whether an element may contain null.
 *
 * @param element Lattice element to check.
 * @return true if null is one of the possible concrete values.
 */
static bool may_be_null(lattice_element_t element) {
    return element.type == LATTICE_NULL || element.can_be_null;
}

/**
 * @brief Checks whether an element has no non-null values.
 *
 * @param element Lattice element to check.
 * @return true if the element is exactly null.
 */
static bool is_pure_null(lattice_element_t element) {
    return element.type == LATTICE_NULL;
}

/**
 * @brief Checks whether a lattice type belongs to the integer domain.
 *
 * @param type Lattice type.
 * @return true for integer and integer interval types.
 */
static bool is_integer_type(lattice_type_t type) {
    return type == LATTICE_INTEGER || type == LATTICE_INTEGER_INTERVAL;
}

/**
 * @brief Checks whether a lattice type belongs to the numeric domain.
 *
 * @param type Lattice type.
 * @return true for numeric, integer-like, and real types.
 */
static bool is_numeric_type(lattice_type_t type) {
    return type == LATTICE_NUMERIC
        || type == LATTICE_INTEGER
        || type == LATTICE_INTEGER_INTERVAL
        || type == LATTICE_REAL;
}

/**
 * @brief Gets integer bounds represented by an integer-like element.
 *
 * @param element Integer-like element.
 * @param out_min Output lower bound.
 * @param out_max Output upper bound.
 * @return true if finite bounds are available, false for unconstrained integers.
 */
static bool get_integer_bounds(lattice_element_t element, int64_t *out_min, int64_t *out_max) {
    if (element.type == LATTICE_INTEGER && element.is_constant) {
        *out_min = element.value.int_value;
        *out_max = element.value.int_value;
        return true;
    }
    if (element.type == LATTICE_INTEGER_INTERVAL) {
        *out_min = element.value.int_interval.min;
        *out_max = element.value.int_interval.max;
        return true;
    }
    return false;
}

/**
 * @brief Computes min without inviting macro nonsense.
 */
static int64_t min_i64(int64_t left, int64_t right) {
    return left < right ? left : right;
}

/**
 * @brief Computes max without inviting macro nonsense.
 */
static int64_t max_i64(int64_t left, int64_t right) {
    return left > right ? left : right;
}

/**
 * @brief Applies the null part of a meet operation to a non-null result.
 *
 * If the non-null intersection is bottom but both operands may be null, the
 * whole meet result is exactly null.
 *
 * @param non_null_result Result of intersecting non-null value domains.
 * @param can_be_null Whether null survived the meet operation.
 * @return Final meet result.
 */
static lattice_element_t finish_meet(lattice_element_t non_null_result, bool can_be_null) {
    if (non_null_result.type == LATTICE_BOTTOM) {
        return can_be_null ? make_null() : make_bottom();
    }
    non_null_result.can_be_null = can_be_null;
    return non_null_result;
}

/**
 * @brief Joins two integer-like lattice elements.
 *
 * @param left First integer-like element.
 * @param right Second integer-like element.
 * @param can_be_null Whether the result may also be null.
 * @return Least upper bound in the integer sublattice.
 */
static lattice_element_t join_integer(lattice_element_t left, lattice_element_t right,
        bool can_be_null) {
    int64_t left_min;
    int64_t left_max;
    int64_t right_min;
    int64_t right_max;

    bool left_has_bounds = get_integer_bounds(left, &left_min, &left_max);
    bool right_has_bounds = get_integer_bounds(right, &right_min, &right_max);

    if (left_has_bounds && right_has_bounds) {
        return make_integer_interval(
            min_i64(left_min, right_min),
            max_i64(left_max, right_max),
            can_be_null
        );
    }

    return make_integer(can_be_null);
}

/**
 * @brief Meets two integer-like lattice elements.
 *
 * @param left First integer-like element.
 * @param right Second integer-like element.
 * @param can_be_null Whether null survived the meet operation.
 * @return Greatest lower bound in the integer sublattice.
 */
static lattice_element_t meet_integer(lattice_element_t left, lattice_element_t right,
        bool can_be_null) {
    int64_t left_min;
    int64_t left_max;
    int64_t right_min;
    int64_t right_max;

    bool left_has_bounds = get_integer_bounds(left, &left_min, &left_max);
    bool right_has_bounds = get_integer_bounds(right, &right_min, &right_max);

    if (left_has_bounds && right_has_bounds) {
        return finish_meet(
            make_integer_interval(
                max_i64(left_min, right_min),
                min_i64(left_max, right_max),
                false
            ),
            can_be_null
        );
    }

    if (left_has_bounds) {
        lattice_element_t result = left;
        result.can_be_null = can_be_null;
        return result;
    }

    if (right_has_bounds) {
        lattice_element_t result = right;
        result.can_be_null = can_be_null;
        return result;
    }

    return make_integer(can_be_null);
}

/**
 * @brief Computes the least upper bound of two lattice elements.
 *
 * Join is used when information from two possible execution paths must be
 * merged. The result is the least element that safely includes all values
 * represented by both operands.
 *
 * @param left First lattice element.
 * @param right Second lattice element.
 * @return Least upper bound of `left` and `right`.
 */
lattice_element_t lattice_join(lattice_element_t left, lattice_element_t right) {
    bool can_be_null = may_be_null(left) || may_be_null(right);

    if (left.type == LATTICE_BOTTOM) {
        right.can_be_null = can_be_null;
        return right;
    }
    if (right.type == LATTICE_BOTTOM) {
        left.can_be_null = can_be_null;
        return left;
    }

    if (is_pure_null(left)) {
        if (is_pure_null(right)) {
            return make_null();
        }
        right.can_be_null = true;
        return right;
    }
    if (is_pure_null(right)) {
        left.can_be_null = true;
        return left;
    }

    if (left.type == LATTICE_TOP || right.type == LATTICE_TOP) {
        return make_top(can_be_null);
    }

    if (is_integer_type(left.type) && is_integer_type(right.type)) {
        return join_integer(left, right, can_be_null);
    }

    if (left.type == LATTICE_REAL && right.type == LATTICE_REAL) {
        if (left.is_constant && right.is_constant && left.value.real_value == right.value.real_value) {
            return make_real_constant(left.value.real_value, can_be_null);
        }
        return make_real(can_be_null);
    }

    if (left.type == LATTICE_BOOLEAN && right.type == LATTICE_BOOLEAN) {
        if (left.is_constant && right.is_constant && left.value.bool_value == right.value.bool_value) {
            return make_boolean_constant(left.value.bool_value, can_be_null);
        }
        return make_boolean(can_be_null);
    }

    if (is_numeric_type(left.type) && is_numeric_type(right.type)) {
        return make_numeric(can_be_null);
    }

    return make_top(can_be_null);
}

/**
 * @brief Computes the greatest lower bound of two lattice elements.
 *
 * Meet is used when a value is constrained by two facts at once. The result is
 * the most precise element that contains only values represented by both
 * operands.
 *
 * @param left First lattice element.
 * @param right Second lattice element.
 * @return Greatest lower bound of `left` and `right`.
 */
lattice_element_t lattice_meet(lattice_element_t left, lattice_element_t right) {
    bool can_be_null = may_be_null(left) && may_be_null(right);

    if (left.type == LATTICE_BOTTOM || right.type == LATTICE_BOTTOM) {
        return make_bottom();
    }

    if (is_pure_null(left)) {
        return may_be_null(right) ? make_null() : make_bottom();
    }
    if (is_pure_null(right)) {
        return may_be_null(left) ? make_null() : make_bottom();
    }

    if (left.type == LATTICE_TOP) {
        right.can_be_null = can_be_null;
        return right;
    }
    if (right.type == LATTICE_TOP) {
        left.can_be_null = can_be_null;
        return left;
    }

    if (left.type == LATTICE_NUMERIC && is_numeric_type(right.type)) {
        right.can_be_null = can_be_null;
        return right;
    }
    if (right.type == LATTICE_NUMERIC && is_numeric_type(left.type)) {
        left.can_be_null = can_be_null;
        return left;
    }

    if (is_integer_type(left.type) && is_integer_type(right.type)) {
        return meet_integer(left, right, can_be_null);
    }

    if (left.type == LATTICE_REAL && right.type == LATTICE_REAL) {
        if (left.is_constant && right.is_constant) {
            if (left.value.real_value == right.value.real_value) {
                return make_real_constant(left.value.real_value, can_be_null);
            }
            return finish_meet(make_bottom(), can_be_null);
        }
        if (left.is_constant) {
            return make_real_constant(left.value.real_value, can_be_null);
        }
        if (right.is_constant) {
            return make_real_constant(right.value.real_value, can_be_null);
        }
        return make_real(can_be_null);
    }

    if (left.type == LATTICE_BOOLEAN && right.type == LATTICE_BOOLEAN) {
        if (left.is_constant && right.is_constant) {
            if (left.value.bool_value == right.value.bool_value) {
                return make_boolean_constant(left.value.bool_value, can_be_null);
            }
            return finish_meet(make_bottom(), can_be_null);
        }
        if (left.is_constant) {
            return make_boolean_constant(left.value.bool_value, can_be_null);
        }
        if (right.is_constant) {
            return make_boolean_constant(right.value.bool_value, can_be_null);
        }
        return make_boolean(can_be_null);
    }

    return finish_meet(make_bottom(), can_be_null);
}
