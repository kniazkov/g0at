/**
 * @file boolean_nodes.c © 2025 Ivan Kniazkov
 * @brief Implementation of the boolean literal expressions (`true` and `false`).
 */

#include "analysis/lattice.h"
#include "codegen/c_control.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "expression.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

/** @brief Boolean literals are temporaries, not part of the numeric interface. */
static bool can_generate_c_code(const node_t *node,
                                const lattice_element_t *value,
                                const c_expression_context_t *context) {
    return context != NULL;
}

/** @brief A boolean literal `true` expression node. */
typedef struct {
    /** @brief Base expression structure from which boolean_true_t inherits. */
    expression_t base;
} boolean_true_t;

/** @brief A boolean literal `false` expression node. */
typedef struct {
    /** @brief Base expression structure from which boolean_false_t inherits. */
    expression_t base;
} boolean_false_t;

/** @brief Implements @ref node_vtbl_t::calculate. */
static const lattice_element_t *
calculate_true(node_t *node, abstract_state_t *state, arena_t *arena) {
    return make_true_element();
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code_true(const node_t *node) {
    return STATIC_STRING(L"true");
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code_true(const node_t *node, source_builder_t *builder, size_t indent) {
    append_static_source(builder, L"true");
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t
generate_bytecode_true(node_t *node, code_builder_t *code, data_builder_t *data) {
    return add_instruction(code, (instruction_t){.opcode = TRUE});
}

/** @brief Virtual table for boolean `true` expressions. */
static node_vtbl_t true_vtbl = {
    .type = NODE_TRUE,
    .analyze_reachability = reachability_literal,
    .is_pure = children_are_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_child_effects,
    .type_name = L"true",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = no_children,
    .get_child = no_child,
    .get_child_tag = no_tags,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = calculate_true,
    .execute = execute_nothing,
    .generate_goat_code = generate_goat_code_true,
    .generate_indented_goat_code = generate_indented_goat_code_true,
    .generate_bytecode = generate_bytecode_true,
    .can_generate_c_code = can_generate_c_code,
    .generate_c_code = c_boolean,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

node_t *create_true_node(arena_t *arena) {
    boolean_true_t *expr = (boolean_true_t *)alloc_zeroed_from_arena(arena, sizeof(boolean_true_t));
    expr->base.base.vtbl = &true_vtbl;
    return &expr->base.base;
}

/** @brief Implements @ref node_vtbl_t::calculate. */
static const lattice_element_t *
calculate_false(node_t *node, abstract_state_t *state, arena_t *arena) {
    return make_false_element();
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code_false(const node_t *node) {
    return STATIC_STRING(L"false");
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code_false(const node_t *node, source_builder_t *builder, size_t indent) {
    append_static_source(builder, L"false");
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t
generate_bytecode_false(node_t *node, code_builder_t *code, data_builder_t *data) {
    return add_instruction(code, (instruction_t){.opcode = FALSE});
}

/** @brief Virtual table for boolean `false` expressions. */
static node_vtbl_t false_vtbl = {
    .type = NODE_FALSE,
    .analyze_reachability = reachability_literal,
    .is_pure = children_are_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_child_effects,
    .type_name = L"false",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = no_children,
    .get_child = no_child,
    .get_child_tag = no_tags,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = calculate_false,
    .execute = execute_nothing,
    .generate_goat_code = generate_goat_code_false,
    .generate_indented_goat_code = generate_indented_goat_code_false,
    .generate_bytecode = generate_bytecode_false,
    .can_generate_c_code = can_generate_c_code,
    .generate_c_code = c_boolean,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

node_t *create_false_node(arena_t *arena) {
    boolean_false_t *expr =
        (boolean_false_t *)alloc_zeroed_from_arena(arena, sizeof(boolean_false_t));
    expr->base.base.vtbl = &false_vtbl;
    return &expr->base.base;
}
