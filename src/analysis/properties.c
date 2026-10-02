/** @file properties.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Postorder property dispatch and cached proof snapshots.
 */
#include "properties.h"

#include "function_summary.h"
#include "graph/expression.h"
#include "graph/node.h"

void classify_node_properties(node_t *node, analysis_collector_t *collector) {
    for (size_t i = 0; i < get_node_child_count(node); i++)
        classify_node_properties(get_node_child(node, i), collector);
    /* Pointwise facts seed the cache; structural methods refine it after children. */
    bool pure = node->vtbl->is_pure(node);
    bool compatible =
        node_has_flag(node, NODE_FLAG_C_COMPATIBLE) || can_generate_c_code_from_node(node, NULL);
    node->flags &= ~(NODE_FLAG_PURE | NODE_FLAG_C_COMPATIBLE);
    if (!node_has_flag(node, NODE_FLAG_UNREACHABLE)) {
        if (pure)
            node->flags |= NODE_FLAG_PURE;
        if (compatible)
            node->flags |= NODE_FLAG_C_COMPATIBLE;
    }
    add_analysis_event(collector, ANALYSIS_NODE_FLAGS, node, NULL, NULL);
    if (node->vtbl->type == NODE_FUNCTION_OBJECT) {
        for (function_summary_t *summary = get_function_summaries(node)->head; summary;
             summary = summary->next)
            add_function_summary_event(collector, summary);
    }
}
