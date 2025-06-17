/**
 * @file visualization.h
 * @copyright 2025 Ivan Kniazkov
 * @brief AST visualization module using GraphViz DOT format
 *
 * Provides interface for converting abstract syntax trees to visual representations
 * through GraphViz toolkit. The module:
 * - Generates DOT graph description from AST
 * - Invokes external `dot` processor
 * - Handles error conditions
 *
 * @warning Requires GraphViz package installed in system
 * @see https://graphviz.org for GraphViz documentation
 */

#pragma once

#include "node.h"

/**
 * @brief Checks if GraphViz is available in system.
 * 
 * Verifies 'dot' command existence by trying to execute it with --version flag.
 * 
 * @return `true` if GraphViz is installed and accessible, `false` if GraphViz is not found
 */
bool is_graphviz_available();

/**
 * @brief Generates a visual representation of the AST as an image file.
 * 
 * This function converts an abstract syntax tree to a graph image using GraphViz.
 * The process involves:
 * 1. Generating DOT format source code from the AST
 * 2. Writing the DOT source to a temporary file
 * 3. Invoking GraphViz's `dot` command to render the image
 * 4. Cleaning up temporary files
 * 
 * @param root_node The root node of the AST to visualize (must not be NULL).
 * @param graph_output_file Path for the output image file (e.g., "ast.png").
 * @return Boolean `true` if image generation succeeded, `false` on any error.
 * 
 * @note The output format is determined by the file extension (e.g., .png, .svg).
 * @warning Requires GraphViz to be installed and accessible in system PATH.
 */
bool generate_image(const node_t* root_node, const char *graph_output_file);
