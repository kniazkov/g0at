/** @file simplification.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Post-analysis rewrites with visible history.
 *
 * Rewriting consumes completed proofs rather than discovering them while mutating
 * the tree. Replacement nodes preserve original syntax for repeat analysis and
 * visualization. A constant result is insufficient if evaluating it has observable
 * effects or may throw; callers must use the appropriate discardability proof.
 * @see restore_graph, can_discard_expression
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
