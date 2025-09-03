/**
 * @file statement_list.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the statement_list node in the abstract syntax tree (AST).
 *
 * This node represents a list of statements enclosed in curly braces.
 * While executing, it creates a new lexical environment (scope).
 */

#include <assert.h>

#include "common_methods.h"
#include "expression.h"
#include "statement.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

/**
 * @struct statement_list_t
 * @brief AST node that stores a list of statements.
 *
 * Holds an array of statements wrapped by curly braces.
 * Execution occurs in a new lexical environment created for the block.
 */
typedef struct {
    /**
     * @brief Base expression structure inherited by statement_list_t.
     *
     * Enables treating this node as an expression during AST traversal and manipulation.
     */
    expression_t base;

    /**
     * @brief Array of pointers to statements in the list.
     */
    statement_t **stmt_list;

    /**
     * @brief Number of statements in the list.
     */
    size_t stmt_count;
} statement_list_t;

/**
 * @brief Returns the number of child statements in the list.
 * @param node Pointer to the statement list node.
 * @return The number of child statements (0 for empty lists).
 */
static size_t get_child_count(const node_t *node) {
    const statement_list_t* list = (const statement_list_t*)node;
    return list->stmt_count;
}

/**
 * @brief Retrieves a specific child statement from the list.
 *
 * Performs bounds-checked access. Valid indices are in the range [0, stmt_count).
 *
 * @param node Pointer to the statement list node.
 * @param index Zero-based statement index.
 * @return Pointer to the child node or NULL if out of range.
 */
static const node_t* get_child(const node_t *node, size_t index) {
    const statement_list_t* list = (const statement_list_t*)node;
    if (index >= list->stmt_count) {
        return NULL;
    }
    return &list->stmt_list[index]->base;
}

/**
 * @brief Converts the statement list node to its Goat language representation.
 *
 * Produces a canonical form:
 * 1) Opening brace '{'
 * 2) Concatenation of all statements separated by a single space
 * 3) Closing brace '}'
 *
 * Empty lists are rendered as "{ }".
 *
 * @param node Pointer to the statement list node.
 * @return string_value_t containing the formatted representation.
 */
static string_value_t generate_goat_code(const node_t *node) {
    const statement_list_t* list = (const statement_list_t*)node;
    if (list->stmt_count == 0) {
        return STATIC_STRING(L"{ }");
    }
    string_builder_t builder;
    init_string_builder(&builder, 16);
    append_char(&builder, L'{');
    for (size_t index = 0; index < list->stmt_count; index++) {
        if (index > 0) {
            append_char(&builder, L' ');
        }
        statement_t *stmt = list->stmt_list[index];
        string_value_t stmt_as_string = stmt->base.vtbl->generate_goat_code(&stmt->base);
        append_string_value(&builder, stmt_as_string);
        FREE_STRING(stmt_as_string);
    }
    return append_char(&builder, L'}');
}

/**
 * @brief Generates properly indented Goat source code for the statement list.
 *
 * Formatting rules:
 * - Braces appear on separate lines.
 * - Nested blocks increase indentation by one tab level.
 * - Statements follow their own indentation rules.
 *
 * @param node Pointer to the statement list node.
 * @param builder Source builder accumulating the output.
 * @param indent Base indentation level (number of tabs) for this block.
 */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
        size_t indent) {
    const statement_list_t* list = (const statement_list_t*)node;
    if (list->stmt_count == 0) {
        append_static_source(builder, L"{ }");
        return;
    }
    append_static_source(builder, L"{");
    for (size_t index = 0; index < list->stmt_count; index++) {
        statement_t *stmt = list->stmt_list[index];
        stmt->base.vtbl->generate_indented_goat_code(&stmt->base, builder, indent + 1);
    }
    add_static_source(builder, indent, L"}");
}

/**
 * @brief Emits bytecode for the statement list.
 *
 * Execution semantics:
 * 1) ENTER — create a new lexical environment (context)
 * 2) emit bytecode for all statements within that environment
 * 3) LEAVE — restore the previous context, preserving the block's result
 *
 * @param node Pointer to the statement list node.
 * @param code Code builder receiving emitted instructions.
 * @param data Data builder for the constant pool.
 * @return Index of the first emitted instruction.
 */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const statement_list_t* list = (const statement_list_t*)node;
    instr_index_t first = add_instruction(code, (instruction_t){ .opcode = ENTER });
    for (size_t index = 0; index < list->stmt_count; index++) {
        statement_t *stmt = list->stmt_list[index];
        stmt->base.vtbl->generate_bytecode(&stmt->base, code, data);
    }
    add_instruction(code, (instruction_t){ .opcode = LEAVE });
    return first;
}

/**
 * @brief Virtual table for the statement_list node operations.
 */
static node_vtbl_t statement_list_vtbl = {
    .type = NODE_STATEMENT_LIST,
    .type_name = L"statement_list",
    .get_data = no_data,
    .get_child_count = get_child_count,
    .get_child = get_child,
    .get_child_tag = no_tags,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode
};

node_t *create_statement_list_node(arena_t *arena) {
    statement_list_t *list = (statement_list_t *)alloc_zeroed_from_arena(
        arena,
        sizeof(statement_list_t)
    );
    list->base.base.vtbl = &statement_list_vtbl;
    return &list->base.base;
}

void fill_statement_list_node(node_t *node, arena_t *arena, statement_t **stmt_list,
        size_t stmt_count) {
    assert(node->vtbl->type == NODE_STATEMENT_LIST);
    statement_list_t *list = (statement_list_t *)node;
    size_t data_size = stmt_count * sizeof(statement_t *);
    list->stmt_list = (statement_t **)alloc_from_arena(arena, data_size);
    memcpy(list->stmt_list, stmt_list, data_size);
    list->stmt_count = stmt_count;
}
