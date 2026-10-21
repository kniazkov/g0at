/** @file test_mutual_recursion.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Recursive group limits and snapshots.
 */
#include "test_mutual_recursion.h"

#include "analysis/analysis.h"
#include "analysis/function_call_graph.h"
#include "analysis/function_return.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <stdio.h>
#include <wchar.h>

bool test_mutual_recursion_limits() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"const a=func(n){return b(n);};"
                      L"const b=func(n){if(n==0)return 1;return c(n-1);};"
                      L"const c=func(n){return a(n);};a(0);"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    string_value_t before = analysis_collector_to_text(collector);
    function_call_graph_t *graph = build_function_call_graph(root, arena, 1024);
    ASSERT(graph->count == 3);
    size_t component = graph->head->component;
    solve_function_recursion(graph, 1);
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        ASSERT(node->component == component && node->recursive);
        ASSERT(node->summary->status == FUNCTION_INCONCLUSIVE);
        ASSERT(node->summary->return_type->type == LATTICE_TOP);
    }
    solve_function_recursion(graph, 64);
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        ASSERT(node->summary->status == FUNCTION_ANALYZED);
        ASSERT(node->summary->return_type->type == LATTICE_INTEGER);
        ASSERT(node->summary->iterations >= 2);
        ASSERT(node->summary->effects == FUNCTION_EFFECT_UNKNOWN);
        ASSERT(node->summary->c_support == FUNCTION_C_UNKNOWN);
    }
    solve_function_recursion(graph, 0);
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        ASSERT(node->summary->status == FUNCTION_INCONCLUSIVE);
        ASSERT(node->summary->return_type->type == LATTICE_TOP);
    }
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
