/**
 * @file statement_expression.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of statement expression node.
 * 
 * This file defines the behavior of the statement expression, which is a special case of statement
 * that wraps an expression. The expression is evaluated, but its result is ignored.
 * This structure is typically used for function calls or operations that don't affect
 * the program's flow or return a value.
 */

#include "statement.h"
#include "expression.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

/**
 * @struct statement_expression_t
 * @brief Represents a statement expression node.
 * 
 * This structure defines a node that wraps an expression in a statement. The
 * expression is evaluated but its result is not used. This is commonly used for expressions
 * like function calls or operations that have side effects but no meaningful value to propagate.
 */
typedef struct {
    /**
     * @brief Base statement structure, from which statement_expression_t inherits.
     * 
     * This allows the structure to be treated as a statement, while still holding an expression
     * within it. It enables the expression to be evaluated as part of a statement,
     * but with no result to propagate.
     */
    statement_t base;

    /**
     * @brief The wrapped expression.
     * 
     * This is the actual expression whose result will be evaluated and ignored.
     * Examples of such expressions include function calls or side-effectful operations.
     */
    expression_t *wrapped;
} statement_expression_t;

/**
 * @brief Converts the statement expression to its string representation.
 * 
 * This function generates the string representation of the statement expression, which is simply
 * the string representation of the wrapped expression followed by a semicolon (`;`), as a statement
 * would appear in source code.
 * 
 * @param node A pointer to the statement expression node.
 * @return A `string_value_t` containing the formatted string representation of the statement.
 */
static string_value_t statement_expression_to_string(const node_t *node) {
    const statement_expression_t *expr = (const statement_expression_t *)node;
    string_builder_t builder;
    string_value_t expr_as_string = expr->wrapped->base.vtbl->to_string(&expr->wrapped->base);
    init_string_builder(&builder, expr_as_string.length + 1);  // +1 for the semicolon
    append_substring(&builder, expr_as_string.data, expr_as_string.length);
    if (expr_as_string.should_free) {
        FREE(expr_as_string.data);
    }
    return append_char(&builder, L';');
}

/**
 * @brief Converts a statement expression node to a statement.
 * @param node A pointer to the statement expression node to be converted.
 * @return A `statement_t*` representing the statement expression node as a statement.
 */
static statement_t *statement_expression_to_statement(node_t *node) {
    return (statement_t *)node;
}

/**
 * @brief Converts a statement expression node to an expression.
 * @param node A pointer to the statement expression node to be converted.
 * @return `NULL`, since a statement expression does not represent a valid expression.
 */
static expression_t *statement_expression_to_expression(node_t *node) {
    return NULL;
}

/**
 * @brief Virtual table for statement expression operations.
 * 
 * This virtual table provides the implementation of operations specific to statement expression
 * nodes, such as converting the statement expression to its string representation, and handling
 * the conversion of the node to both a statement and an expression.
 * 
 * The table includes function pointers for:
 * - `to_string`: Converts the statement expression node to a string.
 * - `to_statement`: Converts the node to a statement.
 * - `to_expression`: Returns `NULL` as statement expressions cannot be used as regular expressions.
 */
static node_vtbl_t statement_expression_vtbl = {
    .type = NODE_STATEMENT_EXPRESSION,
    .to_string = statement_expression_to_string,
    .to_statement = statement_expression_to_statement,
    .to_expression = statement_expression_to_expression
};

node_t *create_statement_expression_node(arena_t *arena, expression_t *wrapped) {
    statement_expression_t *expr = 
        (statement_expression_t *)alloc_from_arena(arena, sizeof(statement_expression_t));
    expr->base.base.vtbl = &statement_expression_vtbl;
    expr->wrapped = wrapped;
    return &expr->base.base;
}
