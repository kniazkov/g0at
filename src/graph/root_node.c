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
#include "codegen/code_builder.h"

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
static string_value_t generate_goat_code(const node_t *node) {
    const root_node_t *root = (const root_node_t *)node;
    string_builder_t builder;
    init_string_builder(&builder, 0);
    string_value_t result = { L"", 0, false };
    for (size_t index = 0; index < root->stmt_count; index++) {
        if (index > 0) {
            append_char(&builder, L' ');
        }
        statement_t *stmt = root->stmt_list[index];
        string_value_t stmt_as_string = stmt->base.vtbl->generate_goat_code(&stmt->base);
        result = append_substring(&builder, stmt_as_string.data, stmt_as_string.length);
        if (stmt_as_string.should_free) {
            FREE(stmt_as_string.data);
        }
    }
    return result;
}

/**
 * @brief Generates bytecode for a root node.
 * 
 * This function generates bytecode for a root node by iterating through all the statements
 * in the `stmt_list` of the root node. For each statement, it calls the `gen_bytecode` function
 * for that statement to generate the corresponding bytecode. The bytecode generation is done
 * for all statements in the list, which represent the top-level operations of the program.
 * 
 * @param node A pointer to the node representing the root node of the AST.
 * @param code A pointer to the `code_builder_t` structure used for generating instructions.
 * @param data A pointer to the `data_builder_t` structure used for managing the data segment.
 */
static void generate_bytecode(const node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const root_node_t *root = (const root_node_t *)node;
    for (size_t index = 0; index < root->stmt_count; index++) {
        statement_t *stmt = root->stmt_list[index];
        stmt->base.vtbl->generate_bytecode(&stmt->base, code, data);
    }
    add_instruction(code, (instruction_t){ .opcode = END });
}

/**
 * @brief Virtual table for root node operations.
 * 
 * This virtual table provides the implementation of operations specific to root nodes in the
 * abstract syntax tree (AST). A root node typically represents the entry point or top-level
 * structure of a program. It often holds the main statements or expressions that will be executed
 * in the program.
 */
static node_vtbl_t root_node_vtbl = {
    .type = NODE_ROOT,
    .generate_goat_code = generate_goat_code,
    .generate_bytecode = generate_bytecode,
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
