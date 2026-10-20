/** @file access.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Conservative access proofs independent of optimization and sampled call signatures.
 */
#pragma once
#include "common/compilation_error.h"
#include "graph/node.h"

/** @brief Rejects definite denials; all other operations retain their VM checks. */
void analyze_access(node_t *root, arena_t *errors, compilation_error_t **list);
/** @brief Whether a function needs runtime access semantics not represented by the native ABI. */
bool function_needs_access_checks(const node_t *function);
/** @brief Primitive receiver proofs may discharge otherwise unknown property accesses. */
bool function_signature_needs_access_checks(const function_summary_t *summary);
/** @brief Folds only closed scalar expressions, preserving declarations, scopes and reads. */
void simplify_access_safe_expressions(node_t *root, arena_t *arena);
