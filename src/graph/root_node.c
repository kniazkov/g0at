/**
 * @file root_node.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the root node in the abstract syntax tree (AST).
 * 
 * This file defines the behavior of the root node, which represents the entry point
 * of the abstract syntax tree. The root node holds a list of statements that are part of the AST
 * and serves as the top-level structure of a program or expression.
 */

#include "statement.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

/**
 * @struct root_node_t
 * @brief Represents the root node of the abstract syntax tree.
 * 
 * This structure defines the root node of the AST, which contains a list of statements that
 * constitute the program or expression. The root node serves as the entry point for traversing
 * the AST, processing statements in sequence.
 */
typedef struct {
    /**
     * @brief Base node structure from which root_node_t inherits.
     * 
     * This allows the root node to be treated as a node in the AST, providing
     * the necessary functionality for tree traversal and manipulation.
     */
    node_t base;

    /**
     * @brief List of statements in the AST.
     * 
     * This is an array of pointers to statement nodes, which are the individual
     * operations or expressions that make up the AST.
     */
    statement_t **stmt_list;

    /**
     * @brief Number of statements in the root node.
     * 
     * This field stores the count of statements in the root node's statement list.
     */
    size_t stmt_count;
} root_node_t;

/**
 * @brief Converts the root node to its string representation.
 * 
 * This function generates the string representation of the root node, which is simply
 * the concatenation of the string representations of all statements in the node's statement list.
 * Each statement is separated by a space, and the result can be used for code generation
 * or debugging purposes.
 * 
 * @param node A pointer to the root node.
 * @return A `string_value_t` containing the formatted string representation of the root node.
 */
static string_value_t root_node_to_string(const node_t *node) {
    const root_node_t *root = (const root_node_t *)node;
    string_builder_t builder;
    init_string_builder(&builder, 0);
    string_value_t result = { L"", 0, false };
    for (size_t i = 0; i < root->stmt_count; i++) {
        if (i > 0) {
            append_char(&builder, L' ');
        }
        statement_t *stmt = root->stmt_list[i];
        string_value_t stmt_as_string = stmt->base.vtbl->to_string(&stmt->base);
        result = append_substring(&builder, stmt_as_string.data, stmt_as_string.length);
        if (stmt_as_string.should_free) {
            FREE(stmt_as_string.data);
        }
    }
    return result;
}

/**
 * @brief Converts a root node to a statement.
 * @param node A pointer to the root node that should be converted to a statement.
 * @return `NULL` since root node cannot be treated as statement.
 */
static statement_t *root_node_to_statement(node_t *node) {
    return NULL;
}

/**
 * @brief Converts a root node to an expression.
 * @param node A pointer to the root node that should be converted to an expression.
 * @return `NULL` since root node cannot be treated as expression.
 */
static expression_t *root_node_to_expression(node_t *node) {
    return NULL;
}

/**
 * @brief Virtual table for root node operations.
 * 
 * This virtual table provides the implementation of operations specific to root nodes in the
 * abstract syntax tree (AST). Root nodes typically represent the entry point or the top-level
 * structure of a program.
 * 
 * The virtual table includes the following function pointers:
 * - `to_string`: Converts the root node to its string representation.
 * - `to_statement`: Converts the root node to a statement (returns `NULL`).
 * - `to_expression`: Converts the root node to an expression (returns `NULL`).
 * 
 * This virtual table enables polymorphic behavior for root nodes, allowing specific operations
 * to be applied to root nodes in the AST, such as string conversion and casting to other node types.
 */
static node_vtbl_t root_node_vtbl = {
    .type = NODE_ROOT,
    .to_string = root_node_to_string,
    .to_statement = root_node_to_statement,
    .to_expression = root_node_to_expression
};


node_t *create_root_node(arena_t *arena, statement_t **stmt_list, size_t stmt_count) {
    root_node_t *root = (root_node_t *)alloc_from_arena(arena, sizeof(root_node_t));
    root->base.vtbl = &root_node_vtbl;
    size_t data_size = stmt_count * sizeof(statement_t *);
    root->stmt_list = (statement_t **)alloc_from_arena(arena, data_size);
    memcpy(root->stmt_list, stmt_list, data_size);
    root->stmt_count = stmt_count;
    return &root->base;
}
