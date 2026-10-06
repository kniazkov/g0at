/** @file function_purity.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Monotone effect propagation over known call dependencies.
 */
#include "function_call_graph.h"
#include "function_effects.h"
#include "graph/declarations.h"
#include "native_builtin.h"

/** @brief Immutable binding reads are stable; the direct capture log retains them. */
static uint32_t own_effects(const function_summary_t *summary) {
    uint32_t effects = summary->direct_effects;
    bool read = false, mutable_read = false;
    for (const function_capture_t *capture = summary->captures; capture; capture = capture->next) {
        if (!(capture->access & FUNCTION_CAPTURE_READ))
            continue;
        read = true;
        if (capture_native_builtin(summary, capture) != NATIVE_BUILTIN_NONE)
            continue;
        mutable_read |= !capture->declarator || capture->declarator == get_builtin_declarator()
                        || capture->declarator->base.vtbl->type != NODE_CONSTANT_DECLARATOR;
    }
    if (read && !mutable_read)
        effects &= ~FUNCTION_EFFECT_EXTERNAL_READ;
    return effects;
}

/** @brief Every syntactic call needs coverage; a missing edge is not evidence of purity. */
static uint32_t called_effects(const function_call_graph_node_t *node) {
    uint32_t effects = FUNCTION_EFFECT_NONE;
    const function_summary_t *summary = node->summary;
    if (summary->has_calls && !summary->effect_calls)
        effects |= FUNCTION_EFFECT_UNKNOWN;
    for (const function_effect_call_t *call = summary->effect_calls; call; call = call->next) {
        if (resolve_native_builtin(get_node_child(call->site, 0)) != NATIVE_BUILTIN_NONE)
            continue;
        bool found = false;
        for (const function_call_edge_t *edge = node->edges; edge; edge = edge->next) {
            if (edge->site != call->site)
                continue;
            found = true;
            effects |= edge->kind == CALL_TARGET_USER && edge->target
                           ? edge->target->summary->effects
                           : FUNCTION_EFFECT_UNKNOWN;
        }
        if (!found)
            effects |= FUNCTION_EFFECT_UNKNOWN;
    }
    return effects;
}

void propagate_function_effects(function_call_graph_t *graph, size_t max_iterations) {
    for (function_call_graph_node_t *node = graph->head; node; node = node->next)
        node->summary->effects = own_effects(node->summary);
    if (!graph->truncated) {
        for (size_t iteration = 0; iteration < max_iterations; iteration++) {
            bool changed = false;
            for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
                uint32_t effects = node->summary->effects | called_effects(node);
                changed |= effects != node->summary->effects;
                node->summary->effects = effects;
            }
            if (!changed)
                return;
        }
    }
    /* Never expose a partial propagation as a proof. */
    for (function_call_graph_node_t *node = graph->head; node; node = node->next)
        node->summary->effects |= FUNCTION_EFFECT_UNKNOWN;
}
