/**
 * @file assignable_expression.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definition of the assignable expression structure.
 * 
 * This file defines the `assignable_expression_t` structure, which represents an assignable
 * expression node in the syntax tree. This structure extends the `expression_t` base node,
 * allowing it to represent expressions that can be assigned a value.
 */

#pragma once

#include "expression.h"

/**
 * @typedef expression_t
 * @brief Forward declaration for the assignable expression structure.
 */
typedef struct assignable_expression_t assignable_expression_t;

/**
 * @struct assignable_expression_t
 * @brief The structure representing an assignable expression node, extending `expression_t`.
 * 
 * This structure adds specific fields to the base `expression_t` for representing expressions
 * that are targets of assignments (e.g., variables or dereferenced pointers).
 */
struct assignable_expression_t {
    /**
     * @brief Base expression node, providing common expression attributes.
     */
    expression_t base;
};
