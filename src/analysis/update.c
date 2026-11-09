/** @file update.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Abstract numeric increment and decrement.
 */
#include "update.h"

#include "addition.h"
#include "subtraction.h"

const lattice_element_t *
lattice_update(arena_t *arena, const lattice_element_t *value, bool decrement) {
    if (value->type == LATTICE_BOTTOM)
        return value;
    if (lattice_may_dispatch_operator(value) || value->type == LATTICE_ARRAY
        || value->type == LATTICE_TYPED_ARRAY)
        return make_top_element();
    if (!is_numeric_lattice_element(value))
        return make_bottom_element();
    const lattice_element_t *one = make_integer_constant_element(arena, 1);
    return decrement ? lattice_subtract(arena, value, one) : lattice_add(arena, value, one);
}
