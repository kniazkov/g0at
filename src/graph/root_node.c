/**
 * @file root_node.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the root node in the abstract syntax tree (AST).
 * 
 * This file defines the behavior of the root node, which represents the entry point
 * of the abstract syntax tree. The root node holds a list of statements that are part of the AST
 * and serves as the top-level structure of a program or expression.
 */

#include "statement.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/linked_list.h"
#include "lib/string_ext.h"
#include "codegen/code_builder.h"
#include "codegen/source_builder.h"

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
     * @brief Linked list of top-level statements.
     *
     * Stores statements in execution order. The linked representation allows
     * analysis and transformation passes to insert additional statements into
     * the program while preserving a valid AST structure.
     */
    list_t *statements;
} root_node_t;

/**
 * @brief Gets the number of child statements in root node.
 * @param node Pointer to the root node (automatically cast to root_node_t*).
 * @return Number of child statements (0 for empty programs).
 */
static size_t get_child_count(const node_t *node) {
    const root_node_t* root = (const root_node_t*)node;
    return root->statements->size;
}

/**
 * @brief Retrieves a specific child statement from root node.
 * 
 * Safely accesses the statement list with bounds checking.
 * Valid indices range from 0 to stmt_count-1.
 * 
 * @param node Pointer to the root node.
 * @param index Zero-based statement position.
 * @return Pointer to statement node or NULL.
 */
static node_t* get_child(const node_t *node, size_t index) {
    const root_node_t* root = (const root_node_t*)node;
    return (node_t*)get_linked_list_value(root->statements, index).ptr;
}

/**
 * @brief Inserts a child statement before another child statement.
 *
 * Searches the root statement list for `before_child` and inserts `new_child`
 * immediately before it. The root node accepts only statements as children;
 * if `before_child` is not found, or `new_child` is not a statement node, the
 * root remains unchanged and the function returns `false`.
 *
 * This is used by static analysis to inject synthetic top-level statements
 * while preserving statement execution order.
 *
 * @param node Pointer to the root node.
 * @param new_child Statement node to insert.
 * @param before_child Existing child statement before which insertion should happen.
 * @return `true` if insertion succeeded, otherwise `false`.
 */
static bool insert_child_before(node_t *node, node_t *new_child, node_t *before_child) {
    if (!is_statement(new_child->vtbl->type)) {
        return false;
    }
    root_node_t* root = (root_node_t*)node;
    list_item_t *item = root->statements->head;
    while(item) {
        if (item->value.ptr == before_child) {
            break;
        }
        item = item->next;
    }
    if (!item) {
        return false;
    }
    insert_item_to_linked_list_before_existing(
        root->statements,
        item,
        (value_t){ .ptr = new_child }
    );
    return true;
}

/**
 * @brief Executes abstract interpretation for the root node.
 *
 * The root node represents the top-level sequence of program statements. Its
 * abstract execution is therefore intentionally linear: the method walks through
 * the statement list in source order and passes the current abstract state from
 * one statement to the next.
 *
 * @param node A pointer to the root node.
 * @param state Input abstract state before executing the program body.
 * @param arena Memory arena used by node execution for lattice values and
 *        analysis-time allocations.
 * @return Abstract state after executing all top-level statements.
 */
static abstract_state_t *execute(node_t *node, abstract_state_t *state, arena_t *arena) {
    const root_node_t *root = (const root_node_t *)node;
    list_item_t *item = root->statements->head;
    while (item) {
        statement_t *stmt = (statement_t*)item->value.ptr;
        state = execute_statement(stmt, state, arena);
        item = item->next;
    }
    return state;
}

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
    string_value_t result = EMPTY_STRING_VALUE;
    list_item_t *item = root->statements->head;
    bool flag = false;
    while (item) {
        if (flag) {
            append_char(&builder, L' ');
        }
        flag = true;
        statement_t *stmt = (statement_t*)item->value.ptr;
        string_value_t stmt_as_string = generate_goat_code_from_statement(stmt);
        result = append_string_value(&builder, stmt_as_string);
        FREE_STRING(stmt_as_string);
        item = item->next;
    }
    return result;
}

/**
 * @brief Generates indented Goat source code for the root node.
 * 
 * This function produces formatted Goat source code with proper indentation.
 * 
 * @param node A pointer to the node.
 * @param builder A pointer to the `source_builder_t` to store the generated output.
 * @param indent The number of tabs used for indentation.
 */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
        size_t indent) {
    const root_node_t *root = (const root_node_t *)node;
    list_item_t *item = root->statements->head;
    while (item) {
        statement_t *stmt = (statement_t*)item->value.ptr;
        generate_indented_goat_code_from_statement(stmt, builder, indent);
        item = item->next;
    }
}

/**
 * @brief Generates bytecode for a root node.
 * 
 * This function generates bytecode for a root node by iterating through all the statements
 * in the `statements` of the root node. For each statement, it calls the `gen_bytecode` function
 * for that statement to generate the corresponding bytecode. The bytecode generation is done
 * for all statements in the list, which represent the top-level operations of the program.
 * 
 * @param node A pointer to the node representing the root node of the AST.
 * @param code A pointer to the `code_builder_t` structure used for generating instructions.
 * @param data A pointer to the `data_builder_t` structure used for managing the data segment.
 * @return The instruction index of the first emitted instruction.
 */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const root_node_t *root = (const root_node_t *)node;
    list_item_t *item = root->statements->head;
    while (item) {
        statement_t *stmt = (statement_t*)item->value.ptr;
        generate_bytecode_from_statement(stmt, code, data);
        item = item->next;
    }
    add_instruction(code, (instruction_t){ .opcode = END });
    return 0;
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
    .type_name = L"root",
    .get_data = no_data,
    .get_child_count = get_child_count,
    .get_child = get_child,
    .get_child_tag = no_tags,
    .insert_child_before = insert_child_before,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = cannot_calculate,
    .execute = execute,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
};

node_t *create_root_node(arena_t *arena, list_t *statements) {
    root_node_t *root = (root_node_t *)alloc_zeroed_from_arena(arena, sizeof(root_node_t));
    root->base.vtbl = &root_node_vtbl;
    root->statements = statements;
    return &root->base;
}
