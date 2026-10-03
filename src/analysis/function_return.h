/** @file function_return.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Isolated return-type proofs for observed function signatures.
 */
#pragma once

typedef struct node_t node_t;

/** @brief Analyzes signatures using generic parameters and unknown captures, without AST facts. */
void analyze_function_return_types(node_t *root);

typedef struct function_call_graph_node_t function_call_graph_node_t;

/** @brief Scans a signature for calls without changing its result or cached node facts. */
void inspect_function_calls(function_call_graph_node_t *node);
