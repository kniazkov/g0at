/** @file test_c_body.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Proof invalidation, recursive dependencies and snapshot isolation.
 */
#include "test_c_body.h"

#include "analysis/analysis.h"
#include "analysis/c_body.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/common_methods.h"
#include "graph/expression.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <stdio.h>
#include <wchar.h>

bool test_c_body_limits() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root =
        parse_analysis_test_program(&memory,
                                    STATIC_STRING(L"const a=func(n){return b(n);};\n"
                                                  L"const b=func(n){return c(n);};\n"
                                                  L"const c=func(n){return n+1;};a(1);"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = {.kind = ANALYSIS_FUNCTION_SUMMARY, .row = 1};
    const analysis_event_t *event = find_analysis_event(collector, NULL, &query);
    ASSERT(event && event->function_summary->c_support == FUNCTION_C_SUPPORTED);
    ASSERT(!event->function_summary->c_blockers);
    function_call_graph_t *graph = build_function_call_graph(root, arena, 1024);
    ASSERT(graph->count == 3 && !graph->truncated);
    string_value_t before = analysis_collector_to_text(collector);
    uint32_t flags = event->node->flags;
    analyze_function_c_bodies(graph, 0);
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        ASSERT(node->summary->c_support == FUNCTION_C_UNKNOWN);
        ASSERT(node->summary->c_blockers & C_BLOCKER_BODY);
        ASSERT(!node->summary->c_expressions);
    }
    analyze_function_c_bodies(graph, 64);
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        ASSERT(node->summary->c_support == FUNCTION_C_SUPPORTED);
    }
    for (function_call_graph_node_t *node = graph->head; node != graph->tail; node = node->next) {
        node->summary->status = FUNCTION_INCONCLUSIVE;
        node->summary->return_type = make_top_element();
    }
    analyze_function_c_bodies(graph, 1);
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        ASSERT(node->summary->c_support != FUNCTION_C_SUPPORTED);
    }
    analyze_function_c_bodies(graph, 64);
    ASSERT(graph->head->summary->return_type->type == LATTICE_INTEGER);
    ASSERT(graph->head->summary->c_support == FUNCTION_C_SUPPORTED);
    graph->truncated = true;
    analyze_function_c_bodies(graph, 64);
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        ASSERT(node->summary->c_support != FUNCTION_C_SUPPORTED);
    }
    graph->truncated = false;
    /* Caller-first order forces invalidation to propagate on later rounds. */
    node_t *body = get_node_child(graph->tail->summary->function, 1);
    node_vtbl_t body_vtbl = *body->vtbl;
    node_vtbl_t rejected = body_vtbl;
    rejected.can_generate_c_code = cannot_generate_c_code;
    body->vtbl = &rejected;
    analyze_function_c_bodies(graph, 1);
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        ASSERT(node->summary->c_support != FUNCTION_C_SUPPORTED);
    }
    analyze_function_c_bodies(graph, 64);
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        ASSERT(node->summary->c_support != FUNCTION_C_SUPPORTED);
    }
    body->vtbl = &body_vtbl;
    analyze_function_c_bodies(graph, 64);
    ASSERT(graph->head->summary->c_support == FUNCTION_C_SUPPORTED);
    ASSERT(event->node->flags == flags);
    string_value_t after = analysis_collector_to_text(collector);
    ASSERT(!wcscmp(before.data, after.data));
    ASSERT(event->function_summary->c_support == FUNCTION_C_SUPPORTED);
    FREE_STRING(before);
    FREE_STRING(after);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_c_body_virtual() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"const a=func(n){if(n<1)return 0;return b(n-1);};\n"
                      L"const b=func(n){if(n<1)return 1;return a(n-1);};a(0);"));
    ASSERT(root);
    options_t *options = create_options();
    ASSERT(!analyze(root, &memory, options, NULL));
    function_call_graph_t *graph = build_function_call_graph(root, arena, 1024);
    ASSERT(graph->count == 2);
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        ASSERT(node->summary->c_support == FUNCTION_C_SUPPORTED);
    }
    node_t *body = get_node_child(graph->tail->summary->function, 1);
    node_t *condition = get_node_child(body, 0);
    node_vtbl_t original = *condition->vtbl;
    node_vtbl_t overridden = original;
    overridden.can_generate_c_code = cannot_generate_c_code;
    condition->vtbl = &overridden;
    analyze_function_c_bodies(graph, 64);
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        ASSERT(node->summary->c_support == FUNCTION_C_UNKNOWN);
        for (const c_expression_proof_t *proof = node->summary->c_expressions; proof;
             proof = proof->next) {
            if (proof->node->vtbl->type == NODE_FUNCTION_CALL) {
                ASSERT(proof->type == C_VALUE_UNKNOWN);
            }
        }
    }
    overridden.can_generate_c_code = NULL;
    analyze_function_c_bodies(graph, 64);
    ASSERT(graph->head->summary->c_support == FUNCTION_C_UNKNOWN);
    condition->vtbl = &original;
    analyze_function_c_bodies(graph, 64);
    ASSERT(graph->head->summary->c_support == FUNCTION_C_SUPPORTED);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
