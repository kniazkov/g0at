/**
 * @file node_type.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Enumeration of node types for the abstract syntax tree (AST).
 */

#pragma once

#include <stdbool.h>

/** @brief Enumeration of node types in the abstract syntax tree (AST). */
typedef enum {
    /** @brief Root node type. */
    NODE_ROOT = 0,

    /** @brief Function argument list node type. */
    NODE_ARGUMENT_LIST,

    /**
     * @brief Function body node type.
     *
     * Unlike a regular statement list, it does not create an additional lexical environment during
     * bytecode generation.
     */
    NODE_FUNCTION_BODY,

    /* Declarators introduce one name each. */

    /** @brief Function argument node type. */
    NODE_ARGUMENT,

    /** @brief Variable declarator node type. */
    NODE_VARIABLE_DECLARATOR,

    /** @brief Constant declarator node type. */
    NODE_CONSTANT_DECLARATOR,

    /* Expressions produce values. */

    /** @brief Statement list node type. */
    NODE_STATEMENT_LIST,

    /**
     * @brief Null literal node type.
     *
     * This node type represents a null literal in the source code, indicating the absence of a
     * value. The node contains no additional data as null is a singleton value.
     */
    NODE_NULL,

    /** @brief Boolean literal `true` node type. */
    NODE_TRUE,

    /** @brief Boolean literal `false` node type. */
    NODE_FALSE,

    /** @brief Static string node type. */
    NODE_STATIC_STRING,

    /** @brief Integer literal node type. */
    NODE_INTEGER,

    /** @brief Real number node type. */
    NODE_REAL,

    /** @brief Variable node type. */
    NODE_VARIABLE,

    /** @brief Parenthesized expression node type. */
    NODE_EXPRESSION_PARENTHESIZED,

    /**
     * @brief Function object expression node type.
     *
     * When evaluated, it produces a callable function capturing its lexical scope.
     */
    NODE_FUNCTION_OBJECT,

    /** @brief Function call node type. */
    NODE_FUNCTION_CALL,

    /** @brief Simple assignment operation node type. */
    NODE_SIMPLE_ASSIGNMENT,

    /** @brief Addition operation node type. */
    NODE_ADDITION,

    /** @brief Subtraction operation node type. */
    NODE_SUBTRACTION,

    /** @brief Multiplication operation node type. */
    NODE_MULTIPLICATION,

    /** @brief Division operation node type. */
    NODE_DIVISION,

    /** @brief Modulo (remainder) operation node type. */
    NODE_MODULO,

    /** @brief Exponentiation operation node type. */
    NODE_POWER,

    /** @brief Less-than comparison node type. */
    NODE_LESS,

    /** @brief Less-than-or-equal comparison node type. */
    NODE_LESS_OR_EQUAL,

    /** @brief Greater-than comparison node type. */
    NODE_GREATER,

    /** @brief Greater-than-or-equal comparison node type. */
    NODE_GREATER_OR_EQUAL,

    /** @brief Equality comparison node type. */
    NODE_EQUAL,

    /** @brief Inequality comparison node type. */
    NODE_NOT_EQUAL,

    /* Statements control execution and side effects. */

    /** @brief Statement expression node type. */
    NODE_STATEMENT_EXPRESSION,

    /** @brief Variable declaration statement node type. */
    NODE_VARIABLE_DECLARATION,

    /** @brief Constant declaration statement node type. */
    NODE_CONSTANT_DECLARATION,

    /** @brief Return statement node type. */
    NODE_RETURN,

    /** @brief Exception-handling statement. */
    NODE_TRY_CATCH,

    /** @brief Conditional branch statement node type. */
    NODE_IF_ELSE,

    /** @brief Classic `for` loop statement node type. */
    NODE_FOR,

    /** @brief `for-in` loop statement node type. */
    NODE_FOR_IN,

    /**
     * @brief `while` loop statement node type.
     *
     * A `while` loop with a condition checked before each iteration.
     */
    NODE_WHILE,

    /**
     * @brief `do-while` loop statement node type.
     *
     * A `do-while` loop with a condition checked after each iteration.
     */
    NODE_DO_WHILE,
} node_type_t;

/**
 * @brief Checks whether a node type represents a declarator.
 * @return `true` if the type is in the declarator range, otherwise `false`.
 */
static inline bool is_declarator(node_type_t type) {
    return type >= NODE_ARGUMENT && type <= NODE_CONSTANT_DECLARATOR;
}

/**
 * @brief Checks whether a node type represents an expression.
 * @return `true` if the type is in the expression range, otherwise `false`.
 */
static inline bool is_expression(node_type_t type) {
    return type >= NODE_STATEMENT_LIST && type <= NODE_NOT_EQUAL;
}

/**
 * @brief Checks whether a node type represents a statement.
 * @return `true` if the type is in the statement range, otherwise `false`.
 */
static inline bool is_statement(node_type_t type) {
    return type >= NODE_STATEMENT_EXPRESSION && type <= NODE_TRY_CATCH;
}

/**
 * @brief Checks whether a node type represents a statement list-like expression.
 * @return `true` for NODE_ROOT, NODE_STATEMENT_LIST, NODE_FUNCTION_BODY, otherwise `false`.
 */
static inline bool is_statement_list(node_type_t type) {
    return type == NODE_ROOT || type == NODE_STATEMENT_LIST || type == NODE_FUNCTION_BODY;
}

/**
 * @brief Checks whether a node type represents a branch or loop statement.
 * @return `true` if the type is a branch or loop statement, otherwise `false`.
 */
static inline bool is_branch_or_loop(node_type_t type) {
    return type >= NODE_IF_ELSE && type <= NODE_DO_WHILE;
}
