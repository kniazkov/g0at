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
node_t *no_child(const node_t *node, size_t index);

/** @brief Implements @ref node_vtbl_t::get_child_tag. */
const wchar_t *no_tags(const node_t *node, size_t index);

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

/** @brief Implements @ref node_vtbl_t::calculate for valueless nodes; returns BOTTOM. */
const lattice_element_t *no_abstract_value(node_t *node, abstract_state_t *state, arena_t *arena);

/** @brief Implements @ref node_vtbl_t::calculate for unsupported operators; evaluates operands,
 * then returns TOP. */
const lattice_element_t *
unknown_abstract_value(node_t *node, abstract_state_t *state, arena_t *arena);

/** @brief Implements @ref node_vtbl_t::execute. */
abstract_state_t *execute_nothing(node_t *node, abstract_state_t *state, arena_t *arena);

/** @brief Implements @ref node_vtbl_t::can_generate_c_code for unsupported nodes. */
bool cannot_generate_c_code(const node_t *node, const lattice_element_t *value);

/** @brief Implements @ref node_vtbl_t::generate_c_code; returns NULL_STRING_VALUE. */
string_value_t no_c_code(const node_t *node);

/** @brief Implements @ref node_vtbl_t::generate_indented_c_code without emitting text. */
void no_indented_c_code(const node_t *node, source_builder_t *builder, size_t indent);

/** @brief Implements @ref node_vtbl_t::generate_bytecode_assign; returns BAD_INSTR_INDEX. */
instr_index_t
no_bytecode_assignment(const node_t *node, code_builder_t *code, data_builder_t *data);

/** @brief Implements @ref node_vtbl_t::generate_bytecode_deferred; no work means complete. */
bool no_deferred_bytecode(const node_t *node, code_builder_t *code, data_builder_t *data);

/** @brief Implements node_vtbl_t::is_pure using already classified children. */
bool children_are_pure(const node_t *node);

/** @brief Implements node_vtbl_t::is_pure when no proof is available. */
bool not_pure(const node_t *node);

/** @brief Implements node_vtbl_t::can_generate_c_code for numeric literals. */
bool numeric_literal_c_code(const node_t *node, const lattice_element_t *value);

/** @brief Implements node_vtbl_t::can_generate_c_code for numeric identity wrappers. */
bool child_c_code(const node_t *node, const lattice_element_t *value);

/** @brief Implements node_vtbl_t::analyze_reachability for literal values. */
const lattice_element_t *
reachability_literal(node_t *node, abstract_state_t **state, analysis_collector_t *collector);

/** @brief Implements node_vtbl_t::analyze_reachability for unsupported control flow. */
const lattice_element_t *
reachability_unknown(node_t *node, abstract_state_t **state, analysis_collector_t *collector);

/** @brief Implements node_vtbl_t::analyze_reachability for a single-child wrapper. */
const lattice_element_t *
visit_reachable_child(node_t *node, abstract_state_t **state, analysis_collector_t *collector);

/** @brief Visits children in order, including dead children after termination. */
const lattice_element_t *
visit_reachable_children(node_t *node, abstract_state_t **state, analysis_collector_t *collector);

/** @brief Shared left-to-right reachability evaluation for arithmetic operations. */
const lattice_element_t *
visit_reachable_binary(node_t *node,
                       abstract_state_t **state,
                       analysis_collector_t *collector,
                       const lattice_element_t *(*operation)(arena_t *,
                                                             const lattice_element_t *,
                                                             const lattice_element_t *));
