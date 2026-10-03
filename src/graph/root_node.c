/**
 * @file root_node.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the root node in the abstract syntax tree (AST).
 */

#include "analysis/abstract_state.h"
#include "codegen/code_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/linked_list.h"
#include "lib/string_ext.h"
#include "statement.h"
#include "statement_sequence.h"

/** @brief The root node of the abstract syntax tree. */
typedef struct {
    /** @brief Base node structure from which root_node_t inherits. */
    node_t base;

    /** @brief Linked list of top-level statements. */
    list_t *statements;
} root_node_t;

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t get_child_count(const node_t *node) {
    const root_node_t *root = (const root_node_t *)node;
    return root->statements->size;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
static node_t *get_child(const node_t *node, size_t index) {
    const root_node_t *root = (const root_node_t *)node;
    return (node_t *)get_linked_list_value(root->statements, index).ptr;
}

/** @brief Implements @ref node_vtbl_t::insert_child_before. */
static bool insert_child_before(node_t *node, node_t *new_child, node_t *before_child) {
    root_node_t *root = (root_node_t *)node;
    return insert_statement_to_list_before(root->statements, new_child, before_child);
}

/** @brief Implements @ref node_vtbl_t::execute. */
static abstract_state_t *execute(node_t *node, abstract_state_t *state, arena_t *arena) {
    const root_node_t *root = (const root_node_t *)node;
    list_item_t *item = root->statements->head;
    while (item && state->control_flow == FLOW_NORMAL) {
        statement_t *stmt = (statement_t *)item->value.ptr;
        state = execute_statement(stmt, state, arena);
        item = item->next;
    }
    return state;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const root_node_t *root = (const root_node_t *)node;
    string_builder_t builder = {0};
    return generate_goat_code_from_statement_list(root->statements, &builder, false);
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const root_node_t *root = (const root_node_t *)node;
    list_item_t *item = root->statements->head;
    while (item) {
        statement_t *stmt = (statement_t *)item->value.ptr;
        generate_indented_goat_code_from_statement(stmt, builder, indent);
        item = item->next;
    }
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const root_node_t *root = (const root_node_t *)node;
    list_item_t *item = root->statements->head;
    while (item) {
        statement_t *stmt = (statement_t *)item->value.ptr;
        generate_bytecode_from_statement(stmt, code, data);
        item = item->next;
    }
    add_instruction(code, (instruction_t){.opcode = END});
    return 0;
}

/** @brief Implements node_vtbl_t::replace_child for a statement sequence. */
static bool replace_child(node_t *node, node_t *old_child, node_t *new_child) {
    return replace_statement_in_list(((root_node_t *)node)->statements, old_child, new_child);
}

/** @brief Virtual table for root node operations. */
static node_vtbl_t root_node_vtbl = {
    .type = NODE_ROOT,
    .analyze_reachability = visit_reachable_children,
    .is_pure = children_are_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_child_effects,
    .type_name = L"root",
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
    .calculate = no_abstract_value,
    .execute = execute,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
    .can_generate_c_code = cannot_generate_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

node_t *create_root_node(arena_t *arena, list_t *statements) {
    root_node_t *root = (root_node_t *)alloc_zeroed_from_arena(arena, sizeof(root_node_t));
    root->base.vtbl = &root_node_vtbl;
    root->statements = statements;
    return &root->base;
}
