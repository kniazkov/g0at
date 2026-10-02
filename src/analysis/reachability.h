/**
 * @file reachability.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Conservative proofs of unreachable AST subtrees.
 */
#pragma once
#include "abstract_state.h"
#include "collector.h"
#include "lattice.h"

/** @brief Recomputes unreachable flags without analyzing deferred function bodies. */
void mark_unreachable_code(node_t *root, arena_t *arena, analysis_collector_t *collector);

/** @brief Dispatches a live node; otherwise marks its subtree. Methods may replace *state. */
const lattice_element_t *
visit_reachable_node(node_t *node, abstract_state_t **state, analysis_collector_t *collector);

/** @brief Marks a dead subtree and records its root. */
void mark_unreachable_subtree(node_t *node, analysis_collector_t *collector);
