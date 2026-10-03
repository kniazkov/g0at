/** @file test_function_purity.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Effect propagation limits and snapshot isolation.
 */
#include "test_function_purity.h"

#include "analysis/analysis.h"
#include "analysis/function_call_graph.h"
#include "analysis/function_effects.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/expression.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <stdio.h>
#include <wchar.h>

bool test_function_purity_limits() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"var x=0;\nconst a=func(n){return b(n);};\n"
                      L"const b=func(n){return c(n);};\nconst c=func(n){x=n;return n;};a(1);"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = {.kind = ANALYSIS_FUNCTION_SUMMARY, .row = 2};
    const analysis_event_t *event = find_analysis_event(collector, NULL, &query);
    ASSERT(event && event->function_summary->effects == FUNCTION_EFFECT_EXTERNAL_WRITE);
    const function_summary_t *snapshot = event->function_summary;
    function_summary_t *live = get_function_summaries(event->node)->head;
    ASSERT(snapshot->effect_calls && live->effect_calls != snapshot->effect_calls);
    ASSERT(live->effect_calls->site == snapshot->effect_calls->site);
    string_value_t before = analysis_collector_to_text(collector);
    function_call_graph_t *graph = build_function_call_graph(root, arena, 1024);
    ASSERT(graph->count == 3 && !graph->truncated);
    propagate_function_effects(graph, 1);
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        ASSERT(node->summary->effects & FUNCTION_EFFECT_UNKNOWN);
        ASSERT(!function_summary_is_pure(node->summary));
    }
    propagate_function_effects(graph, 64);
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        ASSERT(node->summary->effects == FUNCTION_EFFECT_EXTERNAL_WRITE);
        ASSERT(node->summary->c_support == FUNCTION_C_UNKNOWN);
    }
    function_call_edge_t *edges = graph->head->edges;
    graph->head->edges = NULL;
    propagate_function_effects(graph, 64);
    ASSERT(graph->head->summary->effects & FUNCTION_EFFECT_UNKNOWN);
    graph->head->edges = edges;
    propagate_function_effects(graph, 0);
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        ASSERT(!function_summary_is_pure(node->summary));
    }
    /* A fresh syntactic scan invalidates any previously propagated proof. */
    analyze_function_direct_effects(root);
    ASSERT(live->effects == FUNCTION_EFFECT_UNKNOWN);
    graph = build_function_call_graph(root, arena, 1);
    ASSERT(graph->truncated);
    propagate_function_effects(graph, 64);
    ASSERT(live->effects & FUNCTION_EFFECT_UNKNOWN);
    live->effect_calls->site = NULL;
    ASSERT(snapshot->effect_calls->site);
    options->optimization_level = OPTIMIZATION_NONE;
    ASSERT(!analyze(root, &memory, options, NULL));
    string_value_t after = analysis_collector_to_text(collector);
    ASSERT(!wcscmp(before.data, after.data));
    FREE_STRING(before);
    FREE_STRING(after);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
