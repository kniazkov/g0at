/**
 * @file static_string.c
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
    expression_t base;
    wchar_t *value;
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
static string_value_t to_string(const node_t *node) {
    const static_string_t *expr = (const static_string_t *)node;
    return string_to_string_notation(L"", (string_value_t){ expr->value, expr->length, false });
}

/**
 * @brief Virtual table for static string expressions.
 * 
 * Provides the implementation of operations specific to static string expressions,
 * including converting the expression to a string representation.
 */
static node_vtbl_t static_string_vtbl = {
    .type = NODE_STATIC_STRING,
    .to_string = to_string
};

node_t *create_static_string_node(arena_t *arena, const wchar_t *value, size_t length) {
    static_string_t *expr = (static_string_t *)alloc_from_arena(arena, sizeof(static_string_t));
    expr->base.base.vtbl = &static_string_vtbl;
    size_t data_size = (length + 1) * sizeof(wchar_t);
    expr->value = (wchar_t *)alloc_from_arena(arena, data_size);
    expr->length = length;
    memcpy(expr->value, value, data_size);
    return &expr->base.base;
}
