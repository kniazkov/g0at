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
