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
 * @brief A stub generator of Goat code with indentations. Does nothing
 * @param node A pointer to the node.
 * @param builder A pointer to the `source_builder_t` to store the generated output.
 * @param indent The number of tabs used for indentation.
 * @return Always returns `false`.
 */
bool stub_indented_goat_code_generator(const node_t *node, source_builder_t *builder,
    size_t indent);
