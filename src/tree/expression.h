/**
 * @file expression.h
 * @brief Definition of the expression structure.
 * 
 * This file defines the `expression_t` structure, which represents an expression node in
 * the syntax tree of the language.
 */

#pragma once

#include "node.h"

/**
 * @typedef expression_t
 * @brief Forward declaration for the expression structure.
 */
typedef struct expression_t expression_t;

/**
 * @struct expression_t
 * @brief The structure representing an expression node.
 */
struct expression_t {
    node_t base; /**< Base node structure, providing common attributes for all nodes. */
};
