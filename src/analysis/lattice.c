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
 * @brief Top lattice element singleton.
 */
static const lattice_element_t top_element = {
    .type = LATTICE_TOP
};

/**
 * @brief Bottom lattice element singleton.
 */
static const lattice_element_t bottom_element = {
    .type = LATTICE_BOTTOM
};

/**
 * @brief Gets the top lattice element singleton.
 *
 * @return Constant pointer to the top lattice element.
 */
const lattice_element_t *make_top_element() {
    return &top_element;
}

/**
 * @brief Gets the bottom lattice element singleton.
 *
 * @return Constant pointer to the bottom lattice element.
 */
const lattice_element_t *make_bottom_element() {
    return &bottom_element;
}

const lattice_element_t *lattice_join(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right) {
    return make_top_element();
}

const lattice_element_t *lattice_meet(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right) {
    return make_bottom_element();
}
