/**
 * @file variable.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the variable expression node.
 * 
 * This file defines the behavior of the variable expression node, which represents
 * a variable by its name in the syntax tree. 
 */

#include "expression.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

/**
 * @struct variable_t
 * @brief Represents a variable expression node.
 * 
 * This structure defines a variable expression in the syntax tree. A variable refers
 * to a named entity, such as a variable or constant in the source code. The structure
 * extends `expression_t` and includes a field for storing the variable's name.
 */
typedef struct {
    /**
     * @brief Base expression structure from which variable_t inherits.
     */
    expression_t base;

    /**
     * @brief Pointer to the wide-character string representing the variable's name.
     */
    wchar_t *name;

    /**
     * @brief Length of the variable's name in characters (not including the null terminator).
     */
    size_t name_length;
} variable_t;

/**
 * @brief Converts a variable expression to its string representation.
 * 
 * This function converts the given variable expression to its representation as it would
 * appear in the source code (e.g., the variable's name). The resulting string is suitable
 * for embedding in other contexts, such as code generation.
 * 
 * @param node A pointer to the variable expression node.
 * @return A `string_value_t` containing the formatted string representation.
 */
static string_value_t variable_to_string(const node_t *node) {
    const variable_t *expr = (const variable_t *)node;
    return (string_value_t){ expr->name, expr->name_length, false };
}

/**
 * @brief Converts a variable node to a statement.
 * @param node A pointer to the variable node to be converted.
 * @return `NULL`, as a variable does not represent a statement.
 */
static statement_t *variable_to_statement(node_t *node) {
    return NULL;
}

/**
 * @brief Converts a variable node to an expression.
 * @param node A pointer to the variable node to be converted.
 * @return A `expression_t*` representing the variable node as an expression.
 */
static expression_t *variable_to_expression(node_t *node) {
    return (expression_t *)node;
}

/**
 * @brief Virtual table for variable expressions.
 * 
 * This virtual table provides the implementation of operations specific to variable expressions,
 * such as converting the variable expression to a string representation, and handling the
 * conversion of the node to both a statement and an expression.
 * 
 * The table includes function pointers for:
 * - `to_string`: Converts the variable expression node to a string.
 * - `to_statement`: Returns `NULL`, as a variable cannot be used as a statement.
 * - `to_expression`: Converts the variable node to an expression.
 */
static node_vtbl_t variable_vtbl = {
    .type = NODE_VARIABLE,
    .to_string = variable_to_string,
    .to_statement = variable_to_statement,
    .to_expression = variable_to_expression
};

node_t *create_variable_node(arena_t *arena, const wchar_t *name, size_t name_length) {
    variable_t *expr = (variable_t *)alloc_from_arena(arena, sizeof(variable_t));
    expr->base.base.vtbl = &variable_vtbl;
    expr->name = copy_string_to_arena(arena, name, name_length);
    expr->name_length = name_length;
    return &expr->base.base;
}
