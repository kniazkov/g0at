/**
 * @file reachability.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Conservative proofs of unreachable AST subtrees.
 */
#pragma once
#include "collector.h"

/** @brief Recomputes unreachable flags without analyzing deferred function bodies. */
void mark_unreachable_code(node_t *root, arena_t *arena, analysis_collector_t *collector);
