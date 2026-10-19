/** @file array.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Array literal expressions.
 */
#pragma once
#include "expression.h"

/** @brief Ordered elements of an array literal. */
typedef struct {
    expression_t base;       /**< Expression base. */
    expression_t **elements; /**< Element expressions. */
    size_t count;            /**< Number of elements. */
} array_literal_t;

/** @brief Creates an empty literal shell. */
node_t *create_array_node(arena_t *arena);
/** @brief Copies element pointers into the graph arena. */
void set_array_elements(node_t *node, arena_t *arena, expression_t **elements, size_t count);
