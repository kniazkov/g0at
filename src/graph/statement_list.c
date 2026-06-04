/**
 * @file statement_list.c
 * @copyright 2026 Ivan Kniazkov
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
#include "lib/linked_list.h"
#include "lib/string_ext.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

/**
 * @struct statement_list_t
 * @brief AST node that stores a list of statements.
 *
 * Holds statements wrapped by curly braces. Execution occurs in a new lexical
 * environment created for the block.
 */
typedef struct {
    /**
     * @brief Base expression structure.
     *
     * Enables treating this node as an expression during AST traversal,
     * analysis, source generation, and bytecode generation.
     */
    expression_t base;

    /**
     * @brief Linked list of statements in the block.
     *
     * Stores statement nodes in execution order. Each list item contains a
     * pointer to a statement node in its `value.ptr` field.
     */
    list_t *statements;
} statement_list_t;

/**
 * @brief Returns the number of child statements in the list.
 *
 * @param node Pointer to the statement list node.
 * @return Number of child statements, or 0 for an empty list.
 */
static size_t get_child_count(const node_t *node) {
    const statement_list_t* list = (const statement_list_t*)node;
    return list->statements->size;
}

/**
 * @brief Retrieves a specific child statement from the list.
 *
 * Performs bounds-checked indexed access through the linked list. Valid indices
 * are in the range [0, stmt_count).
 *
 * @param node Pointer to the statement list node.
 * @param index Zero-based statement index.
 * @return Pointer to the child statement node, or NULL if index is out of range.
 */
static node_t* get_child(const node_t *node, size_t index) {
    const statement_list_t* list = (const statement_list_t*)node;
    return (node_t*)get_linked_list_value(list->statements, index).ptr;
}

/**
 * @brief Inserts a child statement before another child statement.
 *
 * Searches the statement list for `before_child` and inserts `new_child`
 * immediately before it. A statement list accepts only statement nodes as
 * children. If `before_child` is not found, or `new_child` is not a statement,
 * the list remains unchanged and the function returns `false`.
 *
 * This is used by static analysis to inject synthetic statements while
 * preserving execution order.
 *
 * @param node Pointer to the statement list node.
 * @param new_child Statement node to insert.
 * @param before_child Existing child statement before which insertion should happen.
 * @return `true` if insertion succeeded, otherwise `false`.
 */
static bool insert_child_before(node_t *node, node_t *new_child, node_t *before_child) {
    if (!is_statement(new_child->vtbl->type)) {
        return false;
    }

    statement_list_t* list = (statement_list_t*)node;
    list_item_t *item = list->statements->head;
    while (item) {
        if (item->value.ptr == before_child) {
            break;
        }
        item = item->next;
    }
    if (!item) {
        return false;
    }

    insert_item_to_linked_list_before_existing(
        list->statements,
        item,
        (value_t){ .ptr = new_child }
    );
    return true;
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
    if (list->statements->size == 0) {
        return STATIC_STRING(L"{ }");
    }

    string_builder_t builder;
    init_string_builder(&builder, 16);
    append_char(&builder, L'{');

    bool has_previous = false;
    list_item_t *item = list->statements->head;
    while (item) {
        if (has_previous) {
            append_char(&builder, L' ');
        }
        has_previous = true;

        statement_t *stmt = (statement_t*)item->value.ptr;
        string_value_t stmt_as_string = generate_goat_code_from_statement(stmt);
        append_string_value(&builder, stmt_as_string);
        FREE_STRING(stmt_as_string);

        item = item->next;
    }

    return append_char(&builder, L'}');
}

/**
 * @brief Generates properly indented Goat source code for the statement list.
 *
 * Formatting rules:
 * - Braces appear around the block.
 * - Nested statements increase indentation by one tab level.
 * - Statements follow their own indentation rules.
 *
 * @param node Pointer to the statement list node.
 * @param builder Source builder accumulating the output.
 * @param indent Base indentation level for this block.
 */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
        size_t indent) {
    const statement_list_t* list = (const statement_list_t*)node;
    if (list->statements->size == 0) {
        append_static_source(builder, L"{ }");
        return;
    }

    append_static_source(builder, L"{");

    list_item_t *item = list->statements->head;
    while (item) {
        statement_t *stmt = (statement_t*)item->value.ptr;
        generate_indented_goat_code_from_statement(stmt, builder, indent + 1);
        item = item->next;
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

    list_item_t *item = list->statements->head;
    while (item) {
        statement_t *stmt = (statement_t*)item->value.ptr;
        generate_bytecode_from_statement(stmt, code, data);
        item = item->next;
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
    .insert_child_before = insert_child_before,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = cannot_calculate,
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

void fill_statement_list_node(node_t *node, list_t *statements) {
    assert(node->vtbl->type == NODE_STATEMENT_LIST);
    statement_list_t *list = (statement_list_t *)node;
    list->statements = statements;
}
