/**
 * @file statement_list.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the statement_list node in the abstract syntax tree (AST).
 */

#include <assert.h>

#include "common_methods.h"
#include "expression.h"
#include "statement.h"
#include "statement_sequence.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/linked_list.h"
#include "lib/string_ext.h"
#include "analysis/lattice.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

/**
 * @brief AST node that stores a list of statements.
 *
 * Execution occurs in a new lexical environment created for the block.
 */
typedef struct {
    /** @brief Base expression structure. */
    expression_t base;

    /** @brief Linked list of statements in the block. */
    list_t *statements;
} statement_list_t;

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t get_child_count(const node_t *node) {
    const statement_list_t* list = (const statement_list_t*)node;
    return list->statements->size;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
static node_t* get_child(const node_t *node, size_t index) {
    const statement_list_t* list = (const statement_list_t*)node;
    return (node_t*)get_linked_list_value(list->statements, index).ptr;
}

/** @brief Implements @ref node_vtbl_t::insert_child_before. */
static bool insert_child_before(node_t *node, node_t *new_child, node_t *before_child) {
    statement_list_t* list = (statement_list_t*)node;
    return insert_statement_to_list_before(list->statements, new_child, before_child);
}

/** @brief Implements @ref node_vtbl_t::calculate. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    const statement_list_t* list = (const statement_list_t*)node;
    list_item_t *item = list->statements->head;
    while (item) {
        statement_t *stmt = (statement_t*)item->value.ptr;
        state = execute_statement(stmt, state, arena);
        item = item->next;
    }
    return make_user_defined_object_element();
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const statement_list_t* list = (const statement_list_t*)node;
    string_builder_t builder = { 0 };
    return generate_goat_code_from_statement_list(list->statements, &builder, true);
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
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
 * Execution semantics: 1) ENTER — create a new lexical environment (context) 2) emit bytecode for
 * all statements within that environment 3) LEAVE — restore the previous context, preserving the
 * block's result
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

/** @brief Virtual table for the statement_list node operations. */
static node_vtbl_t statement_list_vtbl = {
    .type = NODE_STATEMENT_LIST,
    .type_name = L"statement_list",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = get_child_count,
    .get_child = get_child,
    .get_child_tag = no_tags,
    .insert_child_before = insert_child_before,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = calculate,
    .execute = execute_nothing,
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
