/**
 * @file common_methods.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implements common methods for the Goat nodes.
 */

#include "common_methods.h"
#include "analysis/lattice.h"
#include "expression.h"

node_display_value_t no_data(const node_t *node) {
    return (node_display_value_t) {
        .text = NULL_STRING_VALUE,
        .kind = NODE_DISPLAY_VALUE_PLAIN
    };
}

size_t no_properties(const node_t *node) {
    return 0;
}

const wchar_t *no_property(const node_t *node, size_t index, node_display_value_t *out_value) {
    *out_value = (node_display_value_t){
        .text = EMPTY_STRING_VALUE,
        .kind = NODE_DISPLAY_VALUE_PLAIN
    };
    return NULL;
}

size_t no_children(const node_t *node) {
    return 0;
}

node_t* no_child(const node_t *node, size_t index) {
    return NULL;
}

const wchar_t* no_tags(const node_t *node, size_t index) {
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

const lattice_element_t *unknown_abstract_value(node_t *node, abstract_state_t *state,
        arena_t *arena) {
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        calculate_expression((expression_t *)get_node_child(node, i), state, arena);
        if (state->control_flow != FLOW_NORMAL) return make_bottom_element();
    }
    return make_top_element();
}

abstract_state_t *execute_nothing(node_t *node, abstract_state_t *state, arena_t *arena) {
    return state;
}

bool cannot_generate_c_code(const node_t *node) {
    return false;
}

string_value_t no_c_code(const node_t *node) {
    return NULL_STRING_VALUE;
}

void no_indented_c_code(const node_t *node, source_builder_t *builder, size_t indent) {
}

instr_index_t no_bytecode_assignment(const node_t *node, code_builder_t *code,
        data_builder_t *data) {
    return BAD_INSTR_INDEX;
}

bool no_deferred_bytecode(const node_t *node, code_builder_t *code, data_builder_t *data) {
    return true;
}
