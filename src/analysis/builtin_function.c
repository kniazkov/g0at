/** @file builtin_function.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Native result domains without executing runtime effects.
 */
#include "builtin_function.h"

#include "lib/integer_math.h"

#include <math.h>

static bool constant(const lattice_element_t *value, double *number) {
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

static bool unknown_type(const lattice_element_t *value) {
    return value->type == LATTICE_TOP || value->type == LATTICE_NOT_NULL;
}

const lattice_element_t *interpret_builtin_atan(abstract_state_t *state,
                                                const lattice_element_t *const *args,
                                                size_t count) {
    for (size_t i = 0; i < 2; i++) {
        if (!is_numeric_lattice_type(args[i]->type) && !unknown_type(args[i]))
            return make_bottom_element();
    }
    double y, x;
    if (constant(args[0], &y) && constant(args[1], &x))
        return make_real_constant_element(state->arena, atan2(y, x));
    return make_real_element();
}

const lattice_element_t *interpret_builtin_print(abstract_state_t *state,
                                                 const lattice_element_t *const *args,
                                                 size_t count) {
    return make_null_element();
}

const lattice_element_t *interpret_builtin_sign(abstract_state_t *state,
                                                const lattice_element_t *const *args,
                                                size_t count) {
    double value;
    if (constant(args[0], &value))
        return make_integer_constant_element(state->arena, value > 0 ? 1 : value < 0 ? -1 : 0);
    if (args[0]->type == LATTICE_INTEGER_RANGE) {
        const integer_range_element_t *range = (const integer_range_element_t *)args[0];
        return make_integer_range_element(state->arena,
                                          range->min > 0   ? 1
                                          : range->min < 0 ? -1
                                                           : 0,
                                          range->max > 0   ? 1
                                          : range->max < 0 ? -1
                                                           : 0);
    }
    if (is_numeric_lattice_type(args[0]->type) || unknown_type(args[0]))
        return make_integer_range_element(state->arena, -1, 1);
    return make_bottom_element();
}

const lattice_element_t *interpret_builtin_sqrt(abstract_state_t *state,
                                                const lattice_element_t *const *args,
                                                size_t count) {
    double value;
    if (constant(args[0], &value))
        return make_real_constant_element(state->arena, sqrt(value));
    if (is_numeric_lattice_type(args[0]->type) || unknown_type(args[0]))
        return make_real_element();
    return make_bottom_element();
}
