/** @file function_effects.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Conservative syntactic effects; no call propagation or reachability claims.
 */
#include "function_effects.h"

#include "function_call_graph.h"
#include "function_return.h"
#include "goat/unicode.h"
#include "graph/common_methods.h"
#include "graph/expression.h"
#include "graph/variable.h"
#include "operator.h"

const function_capture_t *find_function_capture(const function_summary_t *summary,
                                                const declarator_t *declarator,
                                                string_view_t name) {
    for (const function_capture_t *capture = summary->captures; capture; capture = capture->next) {
        if (capture->declarator == declarator && capture->name.length == name.length
            && !MEMCMP(capture->name.data, name.data, name.length))
            return capture;
    }
    return NULL;
}

/** @brief Parameters and block locals belong to their nearest enclosing function. */
void record_function_access(function_summary_t *summary,
                            const node_t *node,
                            uint32_t access,
                            arena_t *arena) {
    if (node->vtbl->type != NODE_VARIABLE) {
        summary->direct_effects |= FUNCTION_EFFECT_UNKNOWN;
        return;
    }
    const variable_t *variable = (const variable_t *)node;
    const declarator_t *decl = variable->declarator;
    if (!decl || variable->dynamic_lookup) {
        summary->direct_effects |= FUNCTION_EFFECT_UNKNOWN;
        return;
    }
    const node_t *owner = &decl->base;
    while (owner && owner->vtbl->type != NODE_FUNCTION_OBJECT)
        owner = owner->parent;
    if (owner == summary->function)
        return;
    if (access & FUNCTION_CAPTURE_READ)
        summary->direct_effects |= FUNCTION_EFFECT_EXTERNAL_READ;
    if (access & FUNCTION_CAPTURE_WRITE)
        summary->direct_effects |= FUNCTION_EFFECT_EXTERNAL_WRITE;
    function_capture_t *capture =
        (function_capture_t *)find_function_capture(summary, decl, variable->name);
    if (!capture) {
        capture = alloc_zeroed_from_arena(arena, sizeof(*capture));
        capture->declarator = decl;
        capture->name = variable->name;
        function_capture_t **tail = &summary->captures;
        while (*tail)
            tail = &(*tail)->next;
        *tail = capture;
    }
    capture->access |= access;
}

void collect_node_direct_effects(const node_t *node, function_summary_t *summary, arena_t *arena) {
    c_expression_context_t context = {.head = summary->c_expressions};
    bool visited = false;
    if (node_has_operator_receiver(node)) {
        const node_t *receiver = get_node_child(node, 0);
        for (const c_expression_proof_t *proof = context.head; proof; proof = proof->next)
            visited |= proof->node == receiver;
        /* Only a complete execution proof makes an absent receiver infeasible.
           Calls and explicit effects retain their separate syntactic coverage. */
        if ((visited || !summary->operator_proofs_complete)
            && node_may_dispatch_operator(node, &context))
            summary->direct_effects |= FUNCTION_EFFECT_UNKNOWN;
    }
    if (node->vtbl->collect_direct_effects)
        node->vtbl->collect_direct_effects(node, summary, arena);
    else
        unknown_direct_effects(node, summary, arena);
}

void analyze_function_direct_effects(node_t *root) {
    if (root->vtbl->type == NODE_FUNCTION_OBJECT) {
        function_summary_set_t *set = get_function_summaries(root);
        for (function_summary_t *summary = set->head; summary; summary = summary->next) {
            summary->direct_effects = FUNCTION_EFFECT_NONE;
            summary->has_calls = false;
            summary->effect_calls = NULL;
            summary->effects = FUNCTION_EFFECT_UNKNOWN;
            summary->captures = NULL;
            collect_node_direct_effects(get_node_child(root, 1), summary, set->arena);
        }
    }
    for (size_t i = 0; i < get_node_child_count(root); i++)
        analyze_function_direct_effects(get_node_child(root, i));
}

void record_function_effect_call(function_summary_t *summary, const node_t *site, arena_t *arena) {
    summary->has_calls = true;
    function_effect_call_t **tail = &summary->effect_calls;
    while (*tail)
        tail = &(*tail)->next;
    *tail = alloc_zeroed_from_arena(arena, sizeof(**tail));
    (*tail)->site = site;
}

void analyze_function_operator_effects(node_t *root,
                                       function_call_graph_t *graph,
                                       size_t max_iterations) {
    for (size_t iteration = 0; iteration < max_iterations; iteration++) {
        size_t count = graph->count, index = 0;
        uint32_t *previous = alloc_from_arena(graph->arena, (count + 1) * sizeof(*previous));
        for (function_call_graph_node_t *node = graph->head; node; node = node->next)
            previous[index++] = node->summary->effects;
        bool changed = analyze_function_operator_expressions(graph);
        analyze_function_direct_effects(root);
        propagate_function_effects(graph, max_iterations);
        changed |= graph->count != count;
        index = 0;
        for (function_call_graph_node_t *node = graph->head; node && index < count;
             node = node->next, index++)
            changed |= previous[index] != node->summary->effects;
        if (!changed)
            return;
    }
    /* Each iteration is independently conservative; no optimistic purity is introduced. */
}
