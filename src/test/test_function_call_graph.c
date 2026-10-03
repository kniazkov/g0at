/** @file test_function_call_graph.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Source-to-collector checks for specialization call dependencies.
 */
#include "test_function_call_graph.h"

#include "analysis/analysis.h"
#include "analysis/function_call_graph.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/expression.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <stdio.h>
#include <wchar.h>

static const analysis_event_t *
at(const analysis_collector_t *collector, analysis_event_kind_t kind, size_t row) {
    analysis_event_query_t query = {.kind = kind, .row = row};
    return find_analysis_event(collector, NULL, &query);
}

static const analysis_event_t *signature_at(const analysis_collector_t *collector,
                                            analysis_event_kind_t kind,
                                            size_t row,
                                            lattice_type_t type) {
    analysis_event_query_t query = {.kind = kind, .row = row};
    for (const analysis_event_t *event = find_analysis_event(collector, NULL, &query); event;
         event = find_analysis_event(collector, event, &query)) {
        if (event->function_summary->parameter_count == 1
            && event->function_summary->parameter_types[0]->type == type)
            return event;
    }
    return NULL;
}

bool test_call_graph_discovery() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"const leaf=func(n){return n;};\n"
                      L"const caller=func(n){if(n==1)return 0;return leaf(n-1);};\n"
                      L"var result=caller(1); caller(1.5);\n"
                      L"const unused=func(n){return leaf(n);};"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    const analysis_event_t *leaf = signature_at(collector, ANALYSIS_CALL_GROUP, 1, LATTICE_INTEGER);
    const analysis_event_t *caller = at(collector, ANALYSIS_CALL_GROUP, 2);
    const analysis_event_t *edge = at(collector, ANALYSIS_CALL_EDGE, 2);
    ASSERT(leaf && caller && edge);
    ASSERT(!leaf->recursive && !caller->recursive && leaf->complete && caller->complete);
    ASSERT(leaf->component != caller->component && leaf->component_size == 1);
    ASSERT(edge->caller_id == caller->caller_id && edge->callee_id == leaf->caller_id);
    ASSERT(edge->callee_summary->function == leaf->node);
    ASSERT(edge->callee_summary->parameter_types[0]->type == LATTICE_INTEGER);
    const analysis_event_t *real = signature_at(collector, ANALYSIS_CALL_EDGE, 2, LATTICE_REAL);
    ASSERT(real && real->callee_summary->parameter_types[0]->type == LATTICE_REAL);
    ASSERT(real->callee_id != edge->callee_id && real->caller_id != edge->caller_id);
    ASSERT(!at(collector, ANALYSIS_CALL_GROUP, 4));
    ASSERT(signature_at(collector, ANALYSIS_FUNCTION_SUMMARY, 1, LATTICE_INTEGER)
               ->function_summary->return_type->type
           == LATTICE_INTEGER);
    ASSERT(caller->function_summary->status == FUNCTION_INCONCLUSIVE);
    ASSERT(at(collector, ANALYSIS_VALUE_WRITE, 3)->value->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(
        ((const integer_constant_element_t *)at(collector, ANALYSIS_VALUE_WRITE, 3)->value)->value
        == 0);
    string_value_t text = analysis_collector_to_text(collector);
    ASSERT(wcsstr(text.data, L": call-edge f") && wcsstr(text.data, L": call-group f"));
    FREE_STRING(text);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

static string_value_t recursive_source() {
    return STATIC_STRING(L"const even=func(n){if(n<1)return true;return odd(n-1);};\n"
                         L"const odd=func(n){if(n<1)return false;return even(n-1);};\n"
                         L"even(2);\n"
                         L"const self=func(n){if(n)return 1;return self(n);}; self(true);");
}

bool test_call_graph_recursion() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(&memory, recursive_source());
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    const analysis_event_t *even = at(collector, ANALYSIS_CALL_GROUP, 1);
    const analysis_event_t *odd = at(collector, ANALYSIS_CALL_GROUP, 2);
    const analysis_event_t *self = at(collector, ANALYSIS_CALL_GROUP, 4);
    ASSERT(even && odd && self);
    ASSERT(even->recursive && odd->recursive && even->component == odd->component);
    ASSERT(even->component_size == 2 && odd->component_size == 2);
    ASSERT(even->complete && odd->complete);
    ASSERT(self->recursive && self->component_size == 1 && self->component != even->component);
    const analysis_event_t *edge = at(collector, ANALYSIS_CALL_EDGE, 4);
    ASSERT(edge && edge->caller_id == edge->callee_id);
    ASSERT(self->function_summary->status == FUNCTION_ANALYZED);
    ASSERT(self->function_summary->return_type->type == LATTICE_INTEGER);
    const analysis_event_t *mutual = at(collector, ANALYSIS_CALL_EDGE, 1);
    ASSERT(mutual && mutual->callee_summary);
    string_value_t before = analysis_collector_to_text(collector);
    get_function_summaries(odd->node)->head->parameter_types[0] = make_real_element();
    ASSERT(mutual->callee_summary->parameter_types[0]->type == LATTICE_INTEGER);
    ASSERT(!analyze(root, &memory, options, NULL));
    string_value_t after = analysis_collector_to_text(collector);
    ASSERT(!wcscmp(before.data, after.data));
    FREE_STRING(before);
    FREE_STRING(after);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_call_graph_bindings() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"const target=func(n){return n;};\n"
                      L"const alias=(target);\n"
                      L"const use=func(n){const print=alias;return print(n);}; use(1);\n"
                      L"var changing=target;\n"
                      L"const unknown=func(n){return changing(n);}; unknown(1);\n"
                      L"const shadow=func(target){return target(1);}; shadow(alias);\n"
                      L"const local=func(n){var t=func(x){return x;};t=func(x){return x+1;};return "
                      L"t(n);}; local(1);\n"
                      L"const dead=func(n){return n;target(n);}; dead(1);\n"
                      L"const native=func(){print(1);}; native();\n"
                      L"const guarded=func(n){if(later)return 0;return target(n);}; guarded(1);\n"
                      L"const later=func(){return 1;};"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    const analysis_event_t *target = at(collector, ANALYSIS_CALL_GROUP, 1);
    const analysis_event_t *alias = at(collector, ANALYSIS_CALL_EDGE, 3);
    ASSERT(target && alias && alias->callee_summary->function == target->node);
    const analysis_event_t *unknown = at(collector, ANALYSIS_CALL_EDGE, 5);
    const analysis_event_t *shadow = at(collector, ANALYSIS_CALL_EDGE, 6);
    ASSERT(unknown && !unknown->callee_summary && !unknown->limited);
    ASSERT(shadow && !shadow->callee_summary);
    ASSERT(!at(collector, ANALYSIS_CALL_GROUP, 5)->complete);
    ASSERT(!at(collector, ANALYSIS_CALL_GROUP, 6)->complete);
    const analysis_event_t *local = at(collector, ANALYSIS_CALL_EDGE, 7);
    ASSERT(local && local->callee_summary);
    ASSERT(local->callee_summary->function->position->begin->column == 48);
    ASSERT(!at(collector, ANALYSIS_CALL_EDGE, 8));
    ASSERT(at(collector, ANALYSIS_CALL_GROUP, 8)->complete);
    ASSERT(!at(collector, ANALYSIS_CALL_EDGE, 9)->callee_summary);
    ASSERT(!at(collector, ANALYSIS_CALL_GROUP, 9)->complete);
    /* A constant function identity is not proof that its declaration has already run. */
    ASSERT(at(collector, ANALYSIS_CALL_EDGE, 10)->callee_summary->function == target->node);
    string_value_t text = analysis_collector_to_text(collector);
    ASSERT(wcsstr(text.data, L"-> unknown") && wcsstr(text.data, L"partial"));
    FREE_STRING(text);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_call_graph_limits() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(&memory, recursive_source());
    ASSERT(root);
    options_t *options = create_options();
    ASSERT(!analyze(root, &memory, options, NULL));
    function_call_graph_t *graph = build_function_call_graph(root, arena, 1);
    ASSERT(graph->count == 1 && graph->truncated);
    ASSERT(graph->head->edges && graph->head->edges->kind == CALL_TARGET_LIMIT);
    analysis_collector_t *collector = create_analysis_collector(arena);
    add_call_graph_events(collector, graph);
    const analysis_event_t *group = at(collector, ANALYSIS_CALL_GROUP, 1);
    const analysis_event_t *edge = at(collector, ANALYSIS_CALL_EDGE, 1);
    ASSERT(group && !group->complete && edge && edge->limited && !edge->callee_summary);
    string_value_t before = analysis_collector_to_text(collector);
    graph->head->component = 999;
    graph->head->summary->parameter_types[0] = make_real_element();
    options->optimization_level = OPTIMIZATION_NONE;
    ASSERT(!analyze(root, &memory, options, NULL));
    string_value_t after = analysis_collector_to_text(collector);
    ASSERT(!wcscmp(before.data, after.data));
    ASSERT(group->function_summary->parameter_types[0]->type == LATTICE_INTEGER);
    ASSERT(group->component != 999);
    add_call_graph_events(NULL, graph);
    FREE_STRING(before);
    FREE_STRING(after);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
