/**
 * @file simple_assignment.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the simple assignment expression node.
 */

#include "analysis/abstract_state.h"
#include "analysis/function_effects.h"
#include "analysis/lattice.h"
#include "analysis/reachability.h"
#include "assignment.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "declarations.h"
#include "graph/variable.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "statement.h"
#include "variable.h"

#include <assert.h>

/** @brief A simple assignment operation expression node. */

typedef struct {
    /** @brief Base binary operation structure from which addition_t inherits. */
    assignment_t base;
} simple_assignment_t;

/** @brief Implements @ref node_vtbl_t::calculate. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    const assignment_t *expr = (const assignment_t *)node;
    const lattice_element_t *value = calculate_expression(expr->right_operand, state, arena);
    if (state->control_flow == FLOW_NORMAL
        && expr->left_operand->base.base.vtbl->type == NODE_VARIABLE) {
        variable_t *var = (variable_t *)(expr->left_operand);
        if (var->declarator && var->declarator != get_builtin_declarator()
            && var->declarator->base.vtbl->type == NODE_CONSTANT_DECLARATOR) {
            state->control_flow = FLOW_UNREACHABLE;
            return make_bottom_element();
        }
        set_in_abstract_state_at(state, var->declarator, value, node);
    }
    return value;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const simple_assignment_t *expr = (const simple_assignment_t *)node;
    string_value_t left = generate_goat_code_from_node(&expr->base.left_operand->base.base);
    string_value_t right = generate_goat_code_from_expression(expr->base.right_operand);
    string_value_t result = format_string(L"%s = %s", left.data, right.data);
    FREE_STRING(left);
    FREE_STRING(right);
    return result;
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const simple_assignment_t *expr = (const simple_assignment_t *)node;
    generate_indented_goat_code_from_node(&expr->base.left_operand->base.base, builder, indent);
    append_static_source(builder, L" = ");
    generate_indented_goat_code_from_expression(expr->base.right_operand, builder, indent);
}

/** @brief Generates bytecode for simple assignment operation. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const simple_assignment_t *expr = (const simple_assignment_t *)node;
    instr_index_t first = generate_bytecode_from_expression(expr->base.right_operand, code, data);
    generate_bytecode_assign_from_node(&expr->base.left_operand->base.base, code, data);
    return first;
}

/** @brief Implements node_vtbl_t::analyze_reachability. */
static const lattice_element_t *
analyze_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    const lattice_element_t *value =
        visit_reachable_node(get_node_child(node, 1), state, collector);
    node_t *target = get_node_child(node, 0);
    if ((*state)->control_flow == FLOW_NORMAL) {
        if (target->vtbl->type == NODE_VARIABLE) {
            set_in_abstract_state(*state, ((variable_t *)target)->declarator, value);
        } else {
            forget_abstract_values(*state);
        }
    }
    return value;
}

/** @brief Implements node_vtbl_t::collect_direct_effects. */
static void
collect_direct_effects(const node_t *node, function_summary_t *summary, arena_t *arena) {
    collect_node_direct_effects(get_node_child(node, 1), summary, arena);
    record_function_access(summary, get_node_child(node, 0), FUNCTION_CAPTURE_WRITE, arena);
}

/** @brief Virtual table for simple assignment operations. */
static node_vtbl_t simple_assignment_vtbl = {
    .type = NODE_SIMPLE_ASSIGNMENT,
    .analyze_reachability = analyze_reachability,
    .is_pure = not_pure,
    .collect_direct_effects = collect_direct_effects,
    .type_name = L"assignment",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = assignment_get_child_count,
    .get_child = assignment_get_child,
    .get_child_tag = assignment_get_tag,
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

expression_t *create_simple_assignment_node(arena_t *arena,
                                            assignable_expression_t *left_operand,
                                            expression_t *right_operand) {
    simple_assignment_t *expr =
        (simple_assignment_t *)alloc_zeroed_from_arena(arena, sizeof(simple_assignment_t));
    expr->base.base.base.vtbl = &simple_assignment_vtbl;
    expr->base.left_operand = left_operand;
    expr->base.right_operand = right_operand;
    return &expr->base.base;
}

declarator_spec_t *create_declarator_from_simple_assignment(const node_t *expr) {
    assert(expr->vtbl->type == NODE_SIMPLE_ASSIGNMENT);
    const simple_assignment_t *assign = (simple_assignment_t *)expr;
    if (assign->base.left_operand->base.base.vtbl->type != NODE_VARIABLE) {
        return NULL;
    }
    declarator_spec_t *decl =
        create_declarator_from_variable(&assign->base.left_operand->base.base);
    decl->initial = assign->base.right_operand;
    return decl;
}
