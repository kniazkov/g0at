/**
 * @file common_methods.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Declarations of common methods for the Goat nodes.
 */

#pragma once

#include "node.h"

/** @brief Implements @ref node_vtbl_t::get_data. */
node_display_value_t no_data(const node_t *node);

/** @brief Implements @ref node_vtbl_t::get_property_count. */
size_t no_properties(const node_t *node);

/** @brief Implements @ref node_vtbl_t::get_property. */
const wchar_t *no_property(const node_t *node, size_t index, node_display_value_t *out_value);

/** @brief Implements @ref node_vtbl_t::get_child_count. */
size_t no_children(const node_t *node);

/** @brief Implements @ref node_vtbl_t::get_child. */
node_t* no_child(const node_t *node, size_t index);

/** @brief Implements @ref node_vtbl_t::get_child_tag. */
const wchar_t* no_tags(const node_t *node, size_t index);

/** @brief Implements @ref node_vtbl_t::insert_child_before. */
bool no_child_insertion(node_t *node, node_t *new_child, node_t *before_child);

/** @brief Implements @ref node_vtbl_t::replace_child. */
bool no_child_replacement(node_t *node, node_t *old_child, node_t *new_child);

/** @brief Implements @ref node_vtbl_t::get_related_count. */
size_t no_related_nodes(const node_t *node);

/** @brief Implements @ref node_vtbl_t::get_related. */
const node_t *no_related_node(const node_t *node, size_t index);

/** @brief Implements @ref node_vtbl_t::get_relation_type. */
relation_type_t no_relation_type(const node_t *node, size_t index);

/** @brief Implements @ref node_vtbl_t::calculate for nodes without values. */
const lattice_element_t *cannot_calculate(node_t *node, abstract_state_t *state, arena_t *arena);

/** @brief Implements @ref node_vtbl_t::execute. */
abstract_state_t *execute_nothing(node_t *node, abstract_state_t *state, arena_t *arena);
