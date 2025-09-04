/**
 * @file binary_operation.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definition of the binary operation expression structure.
 * 
 * This file defines the `binary_operation_t` structure, which represents a binary operation
 * node in the syntax tree. This structure extends the `expression_t` base node, adding references
 * to the left and right operands involved in the binary operation.
 */

#pragma once

#include "expression.h"

/**
 * @typedef binary_operation_t
 * @brief Forward declaration for the binary operation.
 */
typedef struct binary_operation_t binary_operation_t;

/**
 * @struct binary_operation_t
 * @brief The structure representing a binary operation node, extending `expression_t`.
 * 
 * This structure adds the left and right operands to the base `expression_t`, allowing
 * representation of binary operations like addition, multiplication, etc.
 */
struct binary_operation_t {
    /**
     * @brief Base expression node, providing common expression attributes.
     */
    expression_t base;

    /**
     * @brief Pointer to the left operand expression.
     */
    expression_t *left_operand;

    /**
     * @brief Pointer to the right operand expression.
     */
    expression_t *right_operand;
};

/**
 * @brief Gets the number of child nodes for binary operation.
 * 
 * Binary operations always have exactly two children:
 * - Left operand (index 0)
 * - Right operand (index 1)
 * 
 * @param node Pointer to binary operation node (unused, type safety).
 * @return Constant value 2 for all binary operations.
 */
size_t binop_get_child_count(const node_t *node);

/**
 * @brief Retrieves child nodes of binary operation.
 * 
 * Returns:
 * - index 0: left operand node
 * - index 1: right operand node
 * - other indices: NULL
 * 
 * @param node Pointer to binary operation node.
 * @param index Zero-based child position:
 *              - 0: left operand
 *              - 1: right operand
 * @return Pointer to child node or NULL if index invalid.
 */
node_t* binop_get_child(const node_t *node, size_t index);

/**
 * @brief Gets relationship tags for binary operation children.
 * 
 * Returns standardized tags for visualization and debugging:
 * - index 0: "left" operand tag
 * - index 1: "right" operand tag
 * - other indices: NULL
 * 
 * @param node Pointer to parent node (unused, interface consistency).
 * @param index Zero-based child position.
 * @return Static wide string literal or NULL.
 */
const wchar_t* binop_get_tag(const node_t *node, size_t index);

/**
 * @brief Creates a new addition operation node.
 * 
 * This function creates a new addition node by allocating memory for it and initializing
 * its fields, including the left and right operands.
 * 
 * @param arena A pointer to the arena used for memory allocation.
 * @param left_operand The left operand expression of the addition.
 * @param right_operand The right operand expression of the addition.
 * @return A pointer to the newly created addition node.
 */
expression_t *create_addition_node(arena_t *arena, expression_t *left_operand,
    expression_t *right_operand);

/**
 * @brief Creates a new subtraction operation node.
 * 
 * This function creates a new subtraction expression node with the given operands.
 * It allocates memory for the node and sets up its virtual table and operand references.
 * 
 * @param arena The memory arena used to allocate the node.
 * @param left_operand The left operand of the subtraction.
 * @param right_operand The right operand of the subtraction.
 * @return A pointer to the created subtraction node.
 */
expression_t *create_subtraction_node(arena_t *arena, expression_t *left_operand,
        expression_t *right_operand);

/**
 * @brief Creates a multiplication expression node.
 * 
 * Allocates and initializes a multiplication node representing the `*` operation between two
 * operands in the abstract syntax tree.
 * 
 * @param arena The arena allocator used for memory management.
 * @param left_operand The left-hand operand of the multiplication.
 * @param right_operand The right-hand operand of the multiplication.
 * @return A pointer to the newly created multiplication expression node.
 */
expression_t *create_multiplication_node(arena_t *arena, expression_t *left_operand,
        expression_t *right_operand);

/**
 * @brief Creates a division expression node.
 * 
 * Allocates and initializes a division node representing the `/` operation between two
 * operands in the abstract syntax tree.
 * 
 * @param arena The arena allocator used for memory management.
 * @param left_operand The left-hand operand of the division.
 * @param right_operand The right-hand operand of the division.
 * @return A pointer to the newly created division expression node.
 */
expression_t *create_division_node(arena_t *arena, expression_t *left_operand,
        expression_t *right_operand);

/**
 * @brief Creates a modulo (remainder) expression node.
 * 
 * Allocates and initializes a modulo node representing the `%` operation between two
 * operands in the abstract syntax tree.
 * 
 * @param arena The arena allocator used for memory management.
 * @param left_operand The left-hand operand of the modulo.
 * @param right_operand The right-hand operand of the modulo.
 * @return A pointer to the newly created modulo expression node.
 */
expression_t *create_modulo_node(arena_t *arena, expression_t *left_operand,
        expression_t *right_operand);

/**
 * @brief Creates a power (exponentiation) expression node.
 * 
 * Allocates and initializes a power node representing the `**` operation between two
 * operands in the abstract syntax tree.
 * 
 * @param arena The arena allocator used for memory management.
 * @param left_operand The base operand of the exponentiation.
 * @param right_operand The exponent operand of the exponentiation.
 * @return A pointer to the newly created power expression node.
 */
expression_t *create_power_node(arena_t *arena, expression_t *left_operand,
        expression_t *right_operand);

/**
 * @brief Creates a new less-than (`<`) expression node.
 * 
 * @param arena The memory arena to allocate from.
 * @param left_operand The left-hand operand of the comparison.
 * @param right_operand The right-hand operand of the comparison.
 * @return A pointer to the created expression node.
 */
expression_t *create_less_node(arena_t *arena, expression_t *left_operand,
        expression_t *right_operand);
