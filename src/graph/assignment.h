/**
 * @file assignment.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definition of the assignment operation structure.
 * 
 * This file defines the `assignment_t` structure, which represents a base assignment
 * node in the syntax tree. This structure extends the `expression_t` base node,
 * with left operand being assignable (lvalue) and right operand being a regular expression.
 */

#pragma once

#include "expression.h"
#include "assignable_expression.h"

/**
 * @typedef assignment_t
 * @brief Forward declaration for the assignment operation.
 */
typedef struct assignment_t assignment_t;

/**
 * @struct assignment_t
 * @brief The structure representing a base assignment node, extending `expression_t`.
 * 
 * This structure serves as base for all assignment variants (simple, compound),
 * with assignable left operand and expression right operand.
 */
struct assignment_t {
    /**
     * @brief Base expression node, providing common expression attributes.
     */
    expression_t base;

    /**
     * @brief Pointer to the assignable left operand (lvalue).
     */
    assignable_expression_t *left_operand;

    /**
     * @brief Pointer to the right operand expression (rvalue).
     */
    expression_t *right_operand;
};

/**
 * @brief Gets the number of child nodes for assignment operation.
 * 
 * Assignments always have exactly two children:
 * - Left operand (index 0, assignable)
 * - Right operand (index 1, expression)
 * 
 * @param node Pointer to assignment node (unused, type safety).
 * @return Constant value 2 for all assignment operations.
 */
size_t assignment_get_child_count(const node_t *node);

/**
 * @brief Retrieves child nodes of assignment operation.
 * 
 * Returns:
 * - index 0: left operand node (assignable)
 * - index 1: right operand node (expression)
 * - other indices: NULL
 * 
 * @param node Pointer to assignment node.
 * @param index Zero-based child position:
 *              - 0: left operand
 *              - 1: right operand
 * @return Pointer to child node or NULL if index invalid.
 */
const node_t* assignment_get_child(const node_t *node, size_t index);

/**
 * @brief Gets relationship tags for assignment children.
 * 
 * Returns standardized tags for visualization and debugging:
 * - index 0: "target" operand tag
 * - index 1: "value" operand tag
 * - other indices: NULL
 * 
 * @param node Pointer to parent node (unused, interface consistency).
 * @param index Zero-based child position.
 * @return Static wide string literal or NULL.
 */
const wchar_t* assignment_get_tag(const node_t *node, size_t index);

/**
 * @brief Creates a new simple assignment expression node.
 * 
 * @param arena Memory arena for allocation
 * @param left_operand Assignable target expression (lvalue)
 * @param right_operand Value expression (rvalue)
 * @return Pointer to created expression
 */
expression_t *create_simple_assignment_node(arena_t *arena, assignable_expression_t *left_operand,
        expression_t *right_operand);