/** @file function_return.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Isolated return-type proofs for observed function signatures.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct function_summary_t function_summary_t;
typedef struct c_expression_context_t c_expression_context_t;

typedef struct node_t node_t;

/** @brief Analyzes signatures using generic parameters and unknown captures, without AST facts. */
void analyze_function_return_types(node_t *root);

typedef struct function_call_graph_node_t function_call_graph_node_t;

/** @brief Scans a signature for calls without changing its result or cached node facts. */
void inspect_function_calls(function_call_graph_node_t *node);

typedef struct function_call_graph_t function_call_graph_t;
typedef struct abstract_state_t abstract_state_t;
typedef struct lattice_element_t lattice_element_t;

/** @brief Solves recursive type groups; exhaustion leaves TOP/inconclusive. */
void solve_function_recursion(function_call_graph_t *graph, size_t max_iterations);

/** @brief Uses current group approximations; outside calls invalidate the proof. */
const lattice_element_t *interpret_recursive_call(const node_t *site,
                                                  const lattice_element_t *callee,
                                                  const lattice_element_t *const *args,
                                                  size_t count,
                                                  abstract_state_t *state);

/** @brief Reuses generic evaluation for C proofs without changing ordinary observations. */
const lattice_element_t *evaluate_function_c_signature(function_summary_t *summary,
                                                       c_expression_context_t *context,
                                                       bool *incomplete);
