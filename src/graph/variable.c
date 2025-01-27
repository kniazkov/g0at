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
 * @brief Virtual table for variable expressions.
 * 
 * Provides the implementation of operations specific to variable expressions,
 * including converting the expression to a string representation.
 */
static node_vtbl_t variable_vtbl = {
    .type = NODE_VARIABLE,
    .to_string = variable_to_string
};

node_t *create_variable_node(arena_t *arena, const wchar_t *name, size_t name_length) {
    variable_t *expr = (variable_t *)alloc_from_arena(arena, sizeof(variable_t));
    expr->base.base.vtbl = &variable_vtbl;
    expr->name = copy_string_to_arena(arena, name, name_length);
    expr->name_length = name_length;
    return &expr->base.base;
}
