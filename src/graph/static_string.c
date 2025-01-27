/**
 * @file static_string.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the static string expression.
 * 
 * This file defines the behavior of the static string expression, which represents a literal
 * string value in the syntax tree. Static strings are immutable and directly correspond to
 * string literals in the source code.
 */

#include <memory.h>

#include "expression.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

/**
 * @struct static_string_t
 * @brief Represents a static string expression node.
 * 
 * This structure defines a static string expression in the syntax tree. A static string
 * is a literal value that appears in the source code and is immutable. The structure
 * extends `expression_t` and includes additional fields for storing the string's value
 * and its length.
 */
typedef struct {
    /**
     * @brief Base expression structure from which static_string_t inherits.
     */
    expression_t base;

    /**
     * @brief Pointer to the wide-character string data.
     */
    wchar_t *data;

    /**
     * @brief Length of the string in characters (not including the null terminator).
     */
    size_t length;
} static_string_t;

/**
 * @brief Converts a static string expression to its string representation.
 * 
 * This function converts the given static string expression to its representation as it would
 * appear in the source code, including surrounding quotes and escaped characters (e.g., `\n`,
 * `\t`, `\\`). The resulting string is generated in a notation suitable for embedding in other
 * contexts, such as code generation.
 * 
 * @param node A pointer to the static string expression node.
 * @return A `string_value_t` containing the formatted string representation.
 */
static string_value_t static_string_to_string(const node_t *node) {
    const static_string_t *expr = (const static_string_t *)node;
    return string_to_string_notation(L"", (string_value_t){ expr->data, expr->length, false });
}

/**
 * @brief Converts a static string node to a statement.
 * @param node A pointer to the static string node to be converted.
 * @return `NULL`, as a static string does not represent a statement.
 */
static statement_t *static_string_to_statement(node_t *node) {
    return NULL;
}

/**
 * @brief Converts a static string node to an expression.
 * @param node A pointer to the static string node to be converted.
 * @return A `expression_t*` representing the static string node as an expression.
 */
static expression_t *static_string_to_expression(node_t *node) {
    return (expression_t *)node;
}

/**
 * @brief Virtual table for static string expressions.
 * 
 * This virtual table provides the implementation of operations specific to static string
 * expressions, such as converting the static string expression to a string representation,
 * and handling the conversion of the node to both a statement and an expression.
 * 
 * The table includes function pointers for:
 * - `to_string`: Converts the static string expression node to a string.
 * - `to_statement`: Returns `NULL`, as a static string cannot be used as a statement.
 * - `to_expression`: Converts the static string node to an expression.
 */
static node_vtbl_t static_string_vtbl = {
    .type = NODE_STATIC_STRING,
    .to_string = static_string_to_string,
    .to_statement = static_string_to_statement,
    .to_expression = static_string_to_expression
};

node_t *create_static_string_node(arena_t *arena, const wchar_t *data, size_t length) {
    static_string_t *expr = (static_string_t *)alloc_from_arena(arena, sizeof(static_string_t));
    expr->base.base.vtbl = &static_string_vtbl;
    expr->data = copy_string_to_arena(arena, data, length);
    expr->length = length;
    return &expr->base.base;
}
