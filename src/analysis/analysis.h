/**
 * @file analysis.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Declarations for static code analysis functions.
 *
 * This is the orchestration boundary, not just an expression evaluator. Binding and
 * access/iteration checks run even with optimization disabled. Optimized analysis
 * then separates value observations, generic function proofs, reachability and
 * rewrites; callers must inspect diagnostic severity before generating code.
 * @see interpret, build_function_call_graph, simplify_graph
 */

#pragma once

#include "collector.h"

typedef struct compilation_error_t compilation_error_t;
typedef struct node_t node_t;
typedef struct parser_memory_t parser_memory_t;
typedef struct options_t options_t;

/**
 * @brief Assigns scopes and IDs, binds names, then performs abstract interpretation.
 * A NULL collector disables observations.
 * Undeclared variables receive synthetic declarations. Returns accumulated diagnostics.
 */
compilation_error_t *analyze(node_t *root_node,
                             parser_memory_t *memory,
                             options_t *options,
                             analysis_collector_t *collector);
