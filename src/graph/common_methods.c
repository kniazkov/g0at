/**
 * @file common_methods.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implements common methods for the Goat nodes.
 * 
 * This file provides implementations of methods that are shared across multiple 
 * node implementations in the Goat language. These methods are defined separately 
 * to ensure code reuse and maintainability.
 */

#include "common_methods.h"
#include "analysis/lattice.h"

string_value_t no_data(const node_t *node) {
    return NULL_STRING_VALUE;
};

size_t no_properties(const node_t *node) {
    return 0;
}

void no_property(const node_t *node, size_t index,
                 string_view_t *out_key, string_value_t *out_value) {
    *out_key = EMPTY_STRING_VIEW;
    *out_value = EMPTY_STRING_VALUE;
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

const lattice_element_t *cannot_calculate(const node_t *node, arena_t *arena) {
    return make_bottom_element();
}

abstract_state_t *execute_nothing(node_t *node, abstract_state_t *state) {
    return state;
}
