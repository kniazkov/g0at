/**
 * @file statement_list.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the statement_list node in the abstract syntax tree (AST).
 */

#include "statement_list.h"

#include "analysis/abstract_state.h"
#include "analysis/c_body.h"
#include "analysis/lattice.h"
#include "codegen/c_control.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/linked_list.h"
#include "lib/string_ext.h"
#include "statement.h"
#include "statement_sequence.h"

#include <assert.h>

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t get_child_count(const node_t *node) {
    const statement_list_t *list = (const statement_list_t *)node;
    return list->statements->size;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
static node_t *get_child(const node_t *node, size_t index) {
    const statement_list_t *list = (const statement_list_t *)node;
    return (node_t *)get_linked_list_value(list->statements, index).ptr;
}

/** @brief Implements @ref node_vtbl_t::insert_child_before. */
static bool insert_child_before(node_t *node, node_t *new_child, node_t *before_child) {
    statement_list_t *list = (statement_list_t *)node;
    return insert_statement_to_list_before(list->statements, new_child, before_child);
}

/** @brief Implements @ref node_vtbl_t::calculate. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    const statement_list_t *list = (const statement_list_t *)node;
    list_item_t *item = list->statements->head;
    while (item && state->control_flow == FLOW_NORMAL) {
        statement_t *stmt = (statement_t *)item->value.ptr;
        state = execute_statement(stmt, state, arena);
        item = item->next;
    }
    return make_user_defined_object_element();
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const statement_list_t *list = (const statement_list_t *)node;
    string_builder_t builder = {0};
    return generate_goat_code_from_statement_list(list->statements, &builder, true);
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const statement_list_t *list = (const statement_list_t *)node;
    if (list->statements->size == 0) {
        append_static_source(builder, L"{ }");
        return;
    }

    append_static_source(builder, L"{");

    list_item_t *item = list->statements->head;
    while (item) {
        statement_t *stmt = (statement_t *)item->value.ptr;
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
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const statement_list_t *list = (const statement_list_t *)node;
    instr_index_t first = add_instruction(code, (instruction_t){.opcode = ENTER});

    list_item_t *item = list->statements->head;
    while (item) {
        statement_t *stmt = (statement_t *)item->value.ptr;
        generate_bytecode_from_statement(stmt, code, data);
        item = item->next;
    }

    add_instruction(code, (instruction_t){.opcode = LEAVE});
    return first;
}

/** @brief Implements node_vtbl_t::replace_child for a statement sequence. */
static bool replace_child(node_t *node, node_t *old_child, node_t *new_child) {
    return replace_statement_in_list(((statement_list_t *)node)->statements, old_child, new_child);
}

/** @brief Virtual table for the statement_list node operations. */
static node_vtbl_t statement_list_vtbl = {
    .type = NODE_STATEMENT_LIST,
    .analyze_reachability = visit_reachable_children,
    .is_pure = children_are_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_child_effects,
    .type_name = L"statement_list",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = get_child_count,
    .get_child = get_child,
    .get_child_tag = no_tags,
    .insert_child_before = insert_child_before,
    .replace_child = replace_child,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = calculate,
    .execute = execute_nothing,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
    .can_generate_c_code = c_body_children,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = c_emit_block,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

statement_list_t *create_statement_list_node(arena_t *arena) {
    statement_list_t *list =
        (statement_list_t *)alloc_zeroed_from_arena(arena, sizeof(statement_list_t));
    list->base.base.vtbl = &statement_list_vtbl;
    return list;
}

void fill_statement_list_node(statement_list_t *node, list_t *statements) {
    assert(node->base.base.vtbl->type == NODE_STATEMENT_LIST);
    node->statements = statements;
}
