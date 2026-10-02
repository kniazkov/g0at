/**
 * @file binary_operation.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definition of the binary operation expression structure.
 */

#pragma once

#include "expression.h"

typedef struct binary_operation_t binary_operation_t;

/** @brief The structure representing a binary operation node, extending `expression_t`. */
struct binary_operation_t {
    /** @brief Base expression node, providing common expression attributes. */
    expression_t base;

    /** @brief Pointer to the left operand expression. */
    expression_t *left_operand;

    /** @brief Pointer to the right operand expression. */
    expression_t *right_operand;
};

/** @brief Gets the number of child nodes for binary operation. */
size_t binop_get_child_count(const node_t *node);

/**
 * @brief Retrieves child nodes of binary operation.
 *
 * Returns: - index 0: left operand node - index 1: right operand node - other indices: NULL
 * `index`: Zero-based child position: - 0: left operand - 1: right operand
 * @return Pointer to child node or NULL if index invalid.
 */
node_t *binop_get_child(const node_t *node, size_t index);

/**
 * @brief Gets relationship tags for binary operation children.
 *
 * Returns standardized tags for visualization and debugging: - index 0: "left" operand tag - index
 * 1: "right" operand tag - other indices: NULL
 * `index`: Zero-based child position.
 * @return Static wide string literal or NULL.
 */
const wchar_t *binop_get_tag(const node_t *node, size_t index);

/** @brief Creates a new addition operation node. */
expression_t *
create_addition_node(arena_t *arena, expression_t *left_operand, expression_t *right_operand);

/** @brief Creates a new subtraction operation node. */
expression_t *
create_subtraction_node(arena_t *arena, expression_t *left_operand, expression_t *right_operand);

/** @brief Creates a multiplication expression node. */
expression_t *
create_multiplication_node(arena_t *arena, expression_t *left_operand, expression_t *right_operand);

/** @brief Creates a division expression node. */
expression_t *
create_division_node(arena_t *arena, expression_t *left_operand, expression_t *right_operand);

/** @brief Creates a modulo (remainder) expression node. */
expression_t *
create_modulo_node(arena_t *arena, expression_t *left_operand, expression_t *right_operand);

/** @brief Creates a power (exponentiation) expression node. */
expression_t *
create_power_node(arena_t *arena, expression_t *left_operand, expression_t *right_operand);

/** @brief Creates a new less-than (`<`) expression node. */
expression_t *
create_less_node(arena_t *arena, expression_t *left_operand, expression_t *right_operand);

/** @brief Creates a new greater-than (`>`) expression node. */
expression_t *
create_greater_node(arena_t *arena, expression_t *left_operand, expression_t *right_operand);
