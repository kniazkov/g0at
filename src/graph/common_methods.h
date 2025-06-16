/**
 * @file common_methods.h
 * @copyright 2025 Ivan Kniazkov
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
const node_t* no_child(const node_t *node, size_t index);

/**
 * @brief Universal "no child tags" indicator (`get_tag` implementation).
 * @param node Unused parameter (interface compliance).
 * @param index Unused parameter (interface compliance).
 * @return Always returns NULL (no relationship tags available).
 */
const wchar_t* no_tags(const node_t *node, size_t index);

/**
 * @brief A stub generator of Goat code with indentations. Does nothing
 * @param node A pointer to the node.
 * @param builder A pointer to the `source_builder_t` to store the generated output.
 * @param indent The number of tabs used for indentation.
 * @return Always returns `false`.
 */
bool stub_indented_goat_code_generator(const node_t *node, source_builder_t *builder,
    size_t indent);
