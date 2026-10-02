/** @file update_expression.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Prefix and postfix updates of variables.
 */
#pragma once
#include "assignable_expression.h"
#include "unary_expression.h"
/** @brief Creates an update node; the operand must be a variable. */
expression_t *create_prefix_increment_node(arena_t *arena, assignable_expression_t *operand);
expression_t *create_prefix_decrement_node(arena_t *arena, assignable_expression_t *operand);
expression_t *create_postfix_increment_node(arena_t *arena, assignable_expression_t *operand);
expression_t *create_postfix_decrement_node(arena_t *arena, assignable_expression_t *operand);

static inline bool update_is_decrement(node_type_t type) {
    return type == NODE_PREFIX_DECREMENT || type == NODE_POSTFIX_DECREMENT;
}

static inline bool update_is_postfix(node_type_t type) {
    return type == NODE_POSTFIX_INCREMENT || type == NODE_POSTFIX_DECREMENT;
}
