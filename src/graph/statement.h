/**
 * @file statement.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definition of the statement structure.
 * 
 * This file defines the `statement_t` structure, which represents a statement node in the
 * abstract syntax tree (AST). Unlike an expression, which is a value-producing construct,
 * a statement represents an action or operation that performs something.
 * 
 * Statements do not produce a value for further computation. They are used to control the flow of
 * execution, make assignments, or invoke operations. For example, `x = 5;` or `print("Hello");`
 * are statements.
 * 
 * A statement may contain expressions (such as `x = 5;` where `5` is an expression), but it itself
 * does not evaluate to a value.
 * 
 * In contrast, an expression produces a value, like the result of `5 + 10`.
 */

#pragma once

#include "node.h"

/**
 * @struct statement_t
 * @brief The structure representing a statement node.
 * 
 * A statement is a construct that performs some action in the program.
 * Examples include assignments, function calls, loops, and conditionals. Unlike expressions,
 * statements do not produce values that can be used in further computations. They are evaluated
 * for their side effects.
 * 
 * A statement may contain one or more expressions (e.g., the expression `x = 5` is part of
 * a statement), but the statement itself does not return a value.
 */
struct statement_t {
    /**
     * @brief Base node structure, providing common attributes for all nodes.
     * 
     * The `statement_t` structure inherits from `node_t`, which provides
     * common attributes such as node type and virtual method table.
     */
    node_t base;
};

/**
 * @brief Creates a new statement expression node.
 * 
 * This function allocates memory for a new statement expression node, wraps the
 * provided expression, and returns a pointer to the newly created node.
 * 
 * @param arena A pointer to the memory arena for memory allocation.
 * @param wrapped The expression to wrap in the statement expression.
 * @return A pointer to the newly created statement expression node.
 */
statement_t *create_statement_expression_node(arena_t *arena, expression_t *wrapped);
