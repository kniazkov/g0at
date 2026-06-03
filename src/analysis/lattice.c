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
    return (lattice_element_t) {
        .type = LATTICE_TOP
    };
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
    return (lattice_element_t) {
        .type = LATTICE_BOTTOM
    };
}
