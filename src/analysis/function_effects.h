/** @file function_effects.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Direct body effects and lexical captures.
 *
 * Direct effects intentionally cover syntactic branches, even when another pass
 * marks them unreachable. Propagation combines may-effects from callees; unknown
 * coverage blocks purity. A pure result still says nothing about termination or
 * exceptions, so it alone cannot authorize deletion of an expression.
 * @see function_summary_is_pure, can_discard_expression
 */
#pragma once

#include "function_summary.h"

/** @brief Scans all branches of registered bodies; nested bodies are analyzed separately. */
void analyze_function_direct_effects(node_t *root);

/** @brief Looks up a captured binding by declaration identity and name. */
const function_capture_t *find_function_capture(const function_summary_t *summary,
                                                const declarator_t *declarator,
                                                string_view_t name);

/** @brief Dispatches effect collection; missing implementations remain unknown. */
void collect_node_direct_effects(const node_t *node, function_summary_t *summary, arena_t *arena);

/** @brief Records an lvalue access, excluding parameters and locals of the analyzed function. */
void record_function_access(function_summary_t *summary,
                            const node_t *node,
                            uint32_t access,
                            arena_t *arena);

/** @brief Records a call dependency without interpreting its body. */
void record_function_effect_call(function_summary_t *summary, const node_t *site, arena_t *arena);

typedef struct function_call_graph_t function_call_graph_t;

/** @brief Propagates may-effects to a fixed point; missing coverage or limits prevent purity. */
void propagate_function_effects(function_call_graph_t *graph, size_t max_iterations);

/** @brief Refines receiver/call proofs using only independently established callee purity. */
void analyze_function_operator_effects(node_t *root,
                                       function_call_graph_t *graph,
                                       size_t max_iterations);
