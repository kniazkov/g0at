/** @file properties.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Postorder property dispatch and cached proof snapshots.
 */
#include "properties.h"

#include "c_expression.h"
#include "function_summary.h"
#include "graph/common_methods.h"
#include "graph/expression.h"
#include "graph/node.h"

/** @brief Every registered signature must contain a pointwise expression proof. */
static bool expression_is_compatible(const function_summary_set_t *set, const node_t *node) {
    if (!set || !set->head)
        return false;
    for (const function_summary_t *s = set->head; s; s = s->next) {
        c_expression_context_t context = {.head = s->c_expressions};
        if (c_expression_type(&context, node) == C_VALUE_UNKNOWN)
            return false;
    }
    return true;
}

static void
classify(node_t *node, analysis_collector_t *collector, const function_summary_set_t *function) {
    if (node->vtbl->type == NODE_FUNCTION_OBJECT)
        function = get_function_summaries(node);
    uint32_t function_flags = function ? function_summary_flags(function) : 0;
    for (size_t i = 0; i < get_node_child_count(node); i++)
        classify(get_node_child(node, i), collector, function);
    /* Pointwise facts seed the cache; structural methods refine it after children. */
    bool pure = node->vtbl->is_pure(node);
    if (node->vtbl->type == NODE_FUNCTION_CALL && (function_flags & NODE_FLAG_PURE)
        && children_are_pure(node))
        pure = true;
    bool compatible = (!function && node_has_flag(node, NODE_FLAG_C_COMPATIBLE))
                      || (function_flags & NODE_FLAG_C_COMPATIBLE)
                      || expression_is_compatible(function, node)
                      || can_generate_c_code_from_node(node, NULL);
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

void classify_node_properties(node_t *node, analysis_collector_t *collector) {
    classify(node, collector, NULL);
}
