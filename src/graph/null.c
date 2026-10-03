/**
 * @file null.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the null expression.
 */

#include "analysis/lattice.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "expression.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

#include <memory.h>

/**
 * @brief A null expression node.
 *
 * Defines a null expression in the syntax tree. The null value is a singleton that indicates the
 * absence of a meaningful value. The structure extends `expression_t` but contains no additional
 * fields since null requires no additional data storage.
 */
typedef struct {
    /** @brief Base expression structure from which null_t inherits. */
    expression_t base;
} null_t;

/** @brief Implements @ref node_vtbl_t::calculate. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    return make_null_element();
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    return STATIC_STRING(L"null");
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    append_static_source(builder, L"null");
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    return add_instruction(code, (instruction_t){.opcode = NIL});
}

/**
 * @brief Virtual table for null expressions.
 *
 * This virtual table provides the implementation of operations specific to null expressions. It
 * contains function pointers for operations such as converting the null value to a string
 * representation and generating the corresponding bytecode.
 */
static node_vtbl_t null_vtbl = {
    .type = NODE_NULL,
    .analyze_reachability = reachability_literal,
    .is_pure = children_are_pure,
    .collect_direct_effects = collect_child_effects,
    .type_name = L"null",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = no_children,
    .get_child = no_child,
    .get_child_tag = no_tags,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = calculate,
    .execute = execute_nothing,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
    .can_generate_c_code = cannot_generate_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

node_t *create_null_node(arena_t *arena) {
    null_t *expr = (null_t *)alloc_zeroed_from_arena(arena, sizeof(null_t));
    expr->base.base.vtbl = &null_vtbl;
    return &expr->base.base;
}
