/** @file logic.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Logical and integer bitwise expression nodes.
 */
#pragma once
#include "binary_operation.h"
#include "lib/bitwise.h"
bitwise_kind_t node_bitwise_kind(node_type_t type);
expression_t *create_logical_not_node(arena_t *arena, expression_t *left);
expression_t *create_boolean_conversion_node(arena_t *arena, expression_t *left);
expression_t *create_bitwise_not_node(arena_t *arena, expression_t *left);
expression_t *create_logical_and_node(arena_t *arena, expression_t *left, expression_t *right);
expression_t *create_logical_or_node(arena_t *arena, expression_t *left, expression_t *right);
expression_t *create_bitwise_and_node(arena_t *arena, expression_t *left, expression_t *right);
expression_t *create_bitwise_or_node(arena_t *arena, expression_t *left, expression_t *right);
expression_t *create_bitwise_xor_node(arena_t *arena, expression_t *left, expression_t *right);
expression_t *create_shift_left_node(arena_t *arena, expression_t *left, expression_t *right);
expression_t *create_shift_right_node(arena_t *arena, expression_t *left, expression_t *right);
