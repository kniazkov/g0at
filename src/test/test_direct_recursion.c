/** @file test_direct_recursion.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Fixed-point limits, snapshots, and activation-local facts.
 */
#include "test_direct_recursion.h"

#include "analysis/analysis.h"
#include "analysis/function_call_graph.h"
#include "analysis/function_return.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/declarations.h"
#include "graph/expression.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <stdio.h>
#include <wchar.h>

static const analysis_event_t *integer_summary(analysis_collector_t *collector) {
    analysis_event_query_t query = {.kind = ANALYSIS_FUNCTION_SUMMARY};
    for (const analysis_event_t *event = find_analysis_event(collector, NULL, &query); event;
         event = find_analysis_event(collector, event, &query)) {
        if (event->function_summary->parameter_types[0]->type == LATTICE_INTEGER)
            return event;
    }
    return NULL;
}

bool test_direct_recursion_limits() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(
            L"const f=func(n){if(n<1)return 0;if(n==1)return 1;return f(n-1)+f(n-2);}; f(0);"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    const analysis_event_t *event = integer_summary(collector);
    ASSERT(event && event->function_summary->status == FUNCTION_ANALYZED);
    ASSERT(event->function_summary->return_type->type == LATTICE_INTEGER);
    ASSERT(event->function_summary->iterations >= 2);
    ASSERT(event->function_summary->effects == FUNCTION_EFFECT_UNKNOWN);
    ASSERT(event->function_summary->c_support == FUNCTION_C_UNKNOWN);
    string_value_t before = analysis_collector_to_text(collector);
    ASSERT(wcsstr(before.data, L"iterations="));
    function_summary_set_t *set = get_function_summaries(event->node);
    function_call_graph_t *graph = build_function_call_graph(root, arena, 1024);
    size_t node_count = graph->count;
    solve_direct_function_recursion(graph, 1);
    for (function_summary_t *s = set->head; s; s = s->next) {
        ASSERT(s->status == FUNCTION_INCONCLUSIVE && s->return_type->type == LATTICE_TOP);
    }
    solve_direct_function_recursion(graph, 0);
    ASSERT(set->head->status == FUNCTION_INCONCLUSIVE && !set->head->iterations);
    solve_direct_function_recursion(graph, 64);
    ASSERT(set->head->status == FUNCTION_ANALYZED && graph->count == node_count);
    ASSERT(set->head->return_type->type == LATTICE_INTEGER);
    analyze_function_return_types(root);
    graph = build_function_call_graph(root, arena, 1);
    ASSERT(graph->truncated);
    solve_direct_function_recursion(graph, 64);
    ASSERT(set->head->status == FUNCTION_INCONCLUSIVE);
    options->optimization_level = OPTIMIZATION_NONE;
    ASSERT(!analyze(root, &memory, options, NULL));
    ASSERT(!set->head);
    string_value_t after = analysis_collector_to_text(collector);
    ASSERT(!wcscmp(before.data, after.data));
    FREE_STRING(before);
    FREE_STRING(after);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_direct_recursion_captures() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(
            L"var external=3;\nconst f=func(n){var local=n;external=local;return local;};f(1);"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    const node_t *function = integer_summary(collector)->node;
    const declarator_t *parameter =
        (const declarator_t *)get_node_child(get_node_child(function, 0), 0);
    analysis_event_query_t query = {.kind = ANALYSIS_VALUE_WRITE, .row = 1};
    const declarator_t *external = find_analysis_event(collector, NULL, &query)->declarator;
    abstract_state_t *state = create_abstract_state(arena);
    set_in_abstract_state(state, parameter, make_integer_element());
    set_in_abstract_state(state, external, make_integer_constant_element(arena, 3));
    abstract_state_t *copy = clone_abstract_state(state);
    forget_captured_abstract_values(copy, function);
    ASSERT(get_from_abstract_state(copy, parameter)->type == LATTICE_INTEGER);
    ASSERT(get_from_abstract_state(copy, external)->type == LATTICE_TOP);
    ASSERT(get_from_abstract_state(state, external)->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(copy->builtin_bindings_unknown && !state->builtin_bindings_unknown);
    destroy_abstract_state(copy);
    destroy_abstract_state(state);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
