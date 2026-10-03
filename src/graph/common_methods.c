/**
 * @file common_methods.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implements common methods for the Goat nodes.
 */

#include "common_methods.h"

#include "analysis/function_effects.h"
#include "analysis/lattice.h"
#include "analysis/reachability.h"
#include "expression.h"

node_display_value_t no_data(const node_t *node) {
    return (node_display_value_t){.text = NULL_STRING_VALUE, .kind = NODE_DISPLAY_VALUE_PLAIN};
}

size_t no_properties(const node_t *node) {
    return 0;
}

const wchar_t *no_property(const node_t *node, size_t index, node_display_value_t *out_value) {
    *out_value =
        (node_display_value_t){.text = EMPTY_STRING_VALUE, .kind = NODE_DISPLAY_VALUE_PLAIN};
    return NULL;
}

size_t no_children(const node_t *node) {
    return 0;
}

node_t *no_child(const node_t *node, size_t index) {
    return NULL;
}

const wchar_t *no_tags(const node_t *node, size_t index) {
    return NULL;
}

bool no_child_insertion(node_t *node, node_t *new_child, node_t *before_child) {
    return false;
}

bool no_child_replacement(node_t *node, node_t *old_child, node_t *new_child) {
    return false;
}

size_t no_related_nodes(const node_t *node) {
    return 0;
}

const node_t *no_related_node(const node_t *node, size_t index) {
    return NULL;
}

relation_type_t no_relation_type(const node_t *node, size_t index) {
    return RELATION_NONE;
}

const lattice_element_t *no_abstract_value(node_t *node, abstract_state_t *state, arena_t *arena) {
    return make_bottom_element();
}

const lattice_element_t *
unknown_abstract_value(node_t *node, abstract_state_t *state, arena_t *arena) {
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        calculate_expression((expression_t *)get_node_child(node, i), state, arena);
        if (state->control_flow != FLOW_NORMAL)
            return make_bottom_element();
    }
    return make_top_element();
}

abstract_state_t *execute_nothing(node_t *node, abstract_state_t *state, arena_t *arena) {
    return state;
}

bool cannot_generate_c_code(const node_t *node,
                            const lattice_element_t *value,
                            const c_expression_context_t *context) {
    return false;
}

c_generated_expression_t no_c_code(const node_t *node, c_generation_context_t *context) {
    fail_c_generation(context, node, C_GENERATION_UNSUPPORTED);
    return (c_generated_expression_t){0};
}

bool no_indented_c_code(const node_t *node,
                        c_generation_context_t *context,
                        source_builder_t *builder,
                        size_t indent) {
    return fail_c_generation(context, node, C_GENERATION_UNSUPPORTED);
}

instr_index_t
no_bytecode_assignment(const node_t *node, code_builder_t *code, data_builder_t *data) {
    return BAD_INSTR_INDEX;
}

bool no_deferred_bytecode(const node_t *node, code_builder_t *code, data_builder_t *data) {
    return true;
}

bool children_are_pure(const node_t *node) {
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        const node_t *child = get_node_child(node, i);
        /* Creating a closure does not execute its body. */
        if (!node_has_flag(child, NODE_FLAG_UNREACHABLE)
            && child->vtbl->type != NODE_FUNCTION_OBJECT && !node_has_flag(child, NODE_FLAG_PURE))
            return false;
    }
    return true;
}

bool not_pure(const node_t *node) {
    return false;
}

bool numeric_literal_c_code(const node_t *node,
                            const lattice_element_t *value,
                            const c_expression_context_t *context) {
    return true;
}

bool child_c_code(const node_t *node,
                  const lattice_element_t *value,
                  const c_expression_context_t *context) {
    return context ? c_expression_type(context, get_node_child(node, 0)) != C_VALUE_UNKNOWN
                   : node_has_flag(get_node_child(node, 0), NODE_FLAG_C_COMPATIBLE);
}

const lattice_element_t *
reachability_literal(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    return calculate_node(node, *state, (*state)->arena);
}

const lattice_element_t *
reachability_unknown(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    forget_abstract_values(*state);
    return make_top_element();
}

const lattice_element_t *
visit_reachable_child(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    node_t *child = get_node_child(node, 0);
    return child ? visit_reachable_node(child, state, collector) : make_top_element();
}

const lattice_element_t *
visit_reachable_children(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    for (size_t i = 0; i < get_node_child_count(node); i++)
        visit_reachable_node(get_node_child(node, i), state, collector);
    return make_top_element();
}

const lattice_element_t *
visit_reachable_binary(node_t *node,
                       abstract_state_t **state,
                       analysis_collector_t *collector,
                       const lattice_element_t *(*operation)(arena_t *,
                                                             const lattice_element_t *,
                                                             const lattice_element_t *)) {
    const lattice_element_t *left = visit_reachable_node(get_node_child(node, 0), state, collector);
    const lattice_element_t *right =
        visit_reachable_node(get_node_child(node, 1), state, collector);
    const lattice_element_t *value = operation((*state)->arena, left, right);
    if (value->type == LATTICE_BOTTOM)
        (*state)->control_flow = FLOW_UNREACHABLE;
    return value;
}

void collect_child_effects(const node_t *node, function_summary_t *summary, arena_t *arena) {
    for (size_t i = 0; i < get_node_child_count(node); i++)
        collect_node_direct_effects(get_node_child(node, i), summary, arena);
}

void no_direct_effects(const node_t *node, function_summary_t *summary, arena_t *arena) {
}

void unknown_direct_effects(const node_t *node, function_summary_t *summary, arena_t *arena) {
    summary->direct_effects |= FUNCTION_EFFECT_UNKNOWN;
    collect_child_effects(node, summary, arena);
}

node_t *no_simplification(node_t *node, arena_t *arena) {
    return node;
}
