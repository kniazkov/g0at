/**
 * @file common_methods.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Declarations of common methods for the Goat nodes.
 * 
 * This file contains declarations of methods that are shared across multiple 
 * node implementations in the Goat language. These methods are abstracted into 
 * a separate file to promote code reuse and maintainability.
 */

#pragma once

#include "node.h"

/**
 * @brief Universal "no data" stub function for nodes without data.
 * @param node A pointer to the node (maintains signature compatibility).
 * @return Empty `string_value_t`.
 */
string_value_t no_data(const node_t *node);

/**
 * @brief Universal "no properties" indicator for nodes without properties
 *        (`get_property_count` implementation).
 *
 * @param node Unused parameter (maintains interface consistency).
 * @return Always returns 0 (indicating no properties).
 */
size_t no_properties(const node_t *node);

/**
 * @brief Universal "no property" getter stub (`get_property` implementation).
 *
 * This function is used for nodes that do not expose any properties.
 * Instead of leaving the outputs untouched, it explicitly assigns empty
 * values to both @p out_key and @p out_value to ensure predictable behavior.
 *
 * @param node Unused parameter (interface compatibility).
 * @param index Unused parameter (interface compatibility).
 * @param out_key Output pointer that will be set to an empty @ref string_view_t.
 * @param out_value Output pointer that will be set to an empty @ref string_value_t.
 */
void no_property(const node_t *node, size_t index,
                 string_view_t *out_key, string_value_t *out_value);

/**
 * @brief Universal "no children" indicator for leaf nodes (`get_child_count ` implementation).
 * @param node Unused parameter (maintains interface consistency).
 * @return Always returns 0 (indicating no child nodes).
 */
size_t no_children(const node_t *node);

/**
 * @brief Universal "no child" getter stub (`get_child` implementation).
 * @param node Unused parameter (interface compatibility).
 * @param index Unused parameter (interface compatibility).
 * @return Always returns NULL.
 */
node_t* no_child(const node_t *node, size_t index);

/**
 * @brief Universal "no child tags" indicator (`get_tag` implementation).
 * @param node Unused parameter (interface compliance).
 * @param index Unused parameter (interface compliance).
 * @return Always returns NULL (no relationship tags available).
 */
const wchar_t* no_tags(const node_t *node, size_t index);

/**
 * @brief Default implementation for unsupported child insertion.
 *
 * Does not modify the node and always reports failure.
 *
 * @param node A pointer to the parent node.
 * @param new_child A pointer to the child node to insert.
 * @param before_child A pointer to the existing child node.
 * @return Always returns `false`.
 */
bool no_child_insertion(node_t *node, node_t *new_child, node_t *before_child);

/**
 * @brief Default implementation for unsupported child replacement.
 *
 * Does not modify the node and always reports failure.
 *
 * @param node A pointer to the parent node.
 * @param old_child A pointer to the existing child node.
 * @param new_child A pointer to the replacement child node.
 * @return Always returns `false`.
 */
bool no_child_replacement(node_t *node, node_t *old_child, node_t *new_child);

/**
 * @brief Returns zero related nodes.
 *
 * Default implementation for nodes that do not expose non-child relations.
 *
 * @param node A pointer to the node.
 * @return Always returns 0.
 */
size_t no_related_nodes(const node_t *node);

/**
 * @brief Returns no related node.
 *
 * Default implementation for nodes that do not expose non-child relations.
 *
 * @param node A pointer to the node.
 * @param index Zero-based related-node index.
 * @return Always returns NULL.
 */
const node_t *no_related_node(const node_t *node, size_t index);

/**
 * @brief Returns no relation type.
 *
 * Default implementation for nodes that do not expose non-child relations.
 *
 * @param node A pointer to the node.
 * @param index Zero-based related-node index.
 * @return Always returns RELATION_NONE.
 */
relation_type_t no_relation_type(const node_t *node, size_t index);

/**
 * @brief Default abstract-value calculation for nodes that do not produce a value.
 *
 * @param node A pointer to the node.
 * @param arena Memory arena for allocation, unused by the default implementation.
 * @return Bottom lattice element.
 */
const lattice_element_t *cannot_calculate(const node_t *node, arena_t *arena);
