/**
 * @file reachability.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Conservative proofs of unreachable AST subtrees.
 */
#pragma once
#include "abstract_state.h"
#include "collector.h"
#include "lattice.h"

/**
 * @brief Proves which subtrees cannot execute on the current immediate path.
 *
 * Starts with a fresh abstract state rather than accumulated declaration summaries:
 * a value seen somewhere in the program is not necessarily valid at this point.
 * Restores earlier graph replacements and clears stale reachability, purity,
 * C-compatibility and expression-value proofs before visiting the bound AST.
 * Creating a function object does not execute its deferred body in this traversal.
 *
 * The temporary state is destroyed here; abstract values retained by AST nodes
 * belong to @p arena, which must outlive their use. @p collector may be NULL when
 * observation events are not needed. The caller decides whether this pass is safe
 * for the program's access/concurrency semantics; see analyze().
 * @see visit_reachable_node
 */
void mark_unreachable_code(node_t *root, arena_t *arena, analysis_collector_t *collector);

/**
 * @brief Visits a node only while execution can still reach it.
 *
 * A non-normal incoming control flow marks the entire subtree unreachable instead
 * of evaluating it. Otherwise the node's analyze_reachability method models its
 * immediate effects; expression values and C-compatibility proofs are retained
 * only if normal execution continues afterward.
 *
 * @param state Owned, non-NULL state slot. A branch join may destroy the incoming
 * state and replace this slot: callers must reread *state after the visit.
 * @return Abstract expression result, or a node-specific placeholder for a
 * statement. BOTTOM on a skipped path means no value, not boolean false.
 */
const lattice_element_t *
visit_reachable_node(node_t *node, abstract_state_t **state, analysis_collector_t *collector);

/**
 * @brief Marks every descendant unreachable but reports one event for the root.
 *
 * Clears cached immediate expression values without executing the dead code.
 * One ANALYSIS_UNREACHABLE event describes the skipped subtree, not each nested
 * node; source-level tests must select that root's location. The optional
 * collector borrows AST nodes, so their arena must remain alive while reading it.
 */
void mark_unreachable_subtree(node_t *node, analysis_collector_t *collector);
