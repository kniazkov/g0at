/** @file simplification.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Post-analysis rewrites with visible history.
 */
#pragma once
#include "graph/node.h"

/** @brief Replaces proven expressions and branches after analysis has finished. */
void simplify_graph(node_t *root, arena_t *arena);
/** @brief Restores archived originals before binding and analyzing the graph again. */
void restore_graph(node_t *root);
/** @brief Implements node_vtbl_t::simplify for scalar expressions. */
node_t *simplify_constant_expression(node_t *node, arena_t *arena);
/** @brief Proves evaluation removable; purity alone does not exclude calls or throws. */
bool can_discard_expression(const node_t *node);
