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
