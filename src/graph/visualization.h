/**
 * @file visualization.h
 * @copyright 2026 Ivan Kniazkov
 * @brief AST visualization module using GraphViz DOT format
 */

#pragma once

#include "node.h"

/**
 * @brief Checks if GraphViz is available in system.
 * @return `true` if GraphViz is installed and accessible, `false` if GraphViz is not found
 */
bool is_graphviz_available();

/**
 * @brief Generates a visual representation of the AST as an image file.
 * `root_node`: The root node of the AST to visualize (must not be NULL).
 * @return Boolean `true` if image generation succeeded, `false` on any error.
 * @warning Requires GraphViz to be installed and accessible in system PATH.
 */
bool generate_image(const node_t *root_node, const char *graph_output_file);

/** @brief Builds DOT without invoking Graphviz; caller frees the returned string. */
string_value_t generate_graph_dot(const node_t *root_node);
