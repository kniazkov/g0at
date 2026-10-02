/** @file unary_expression.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared base for expressions with one operand.
 */
#pragma once
#include "expression.h"

typedef struct {
    expression_t base;
    expression_t *operand;
} unary_expression_t;

/** @brief Creates a unary numeric identity node. */
expression_t *create_unary_plus_node(arena_t *arena, expression_t *operand);
/** @brief Creates a unary numeric negation node. */
expression_t *create_unary_minus_node(arena_t *arena, expression_t *operand);
