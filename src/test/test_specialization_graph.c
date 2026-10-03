/** @file test_specialization_graph.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Specialization views must not mutate or overstate shared proof caches.
 */
#include "test_specialization_graph.h"

#include "analysis/analysis.h"
#include "analysis/function_summary.h"
#include "analysis/properties.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/expression.h"
#include "graph/visualization.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <stdio.h>
#include <wchar.h>

static size_t occurrences(const wchar_t *text, const wchar_t *part) {
    size_t count = 0;
    while ((text = wcsstr(text, part))) {
        count++;
        text += wcslen(part);
    }
    return count;
}

static size_t nodes(const node_t *node) {
    size_t count = 1;
    for (size_t i = 0; i < get_node_child_count(node); i++)
        count += nodes(get_node_child(node, i));
    return count;
}

bool test_specialization_graph() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"const f=func(n){if(n<1)return 0;if(n==1)return 1;return f(n-1)+f(n-2);};\n"
                      L"var v=abs(10);var x=f(v);print(x);"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = {.kind = ANALYSIS_FUNCTION_SUMMARY, .row = 1};
    const analysis_event_t *event = find_analysis_event(collector, NULL, &query);
    ASSERT(event);
    node_t *function = (node_t *)event->node;
    function_summary_set_t *set = get_function_summaries(function);
    const function_summary_t *view = select_function_c_view(set);
    ASSERT(view && view->parameter_types[0]->type == LATTICE_REAL);
    ASSERT(function->flags & NODE_FLAG_PURE);
    /* Discovery also contains an unknown-argument profile; it must not acquire C support. */
    ASSERT(!(function->flags & NODE_FLAG_C_COMPATIBLE));
    string_value_t before = analysis_collector_to_text(collector);
    uint32_t flags = function->flags;
    string_value_t dot = generate_graph_dot(function);
    ASSERT(wcsstr(dot.data, L"C view"));
    ASSERT(wcsstr(dot.data, L"(real)"));
    ASSERT(wcsstr(dot.data, L"C=supported") && wcsstr(dot.data, L"C=unknown"));
    ASSERT(occurrences(dot.data, L"color=forestgreen") == nodes(function));
    ASSERT(occurrences(dot.data, L"style=\"rounded,filled\" fillcolor=\"#f2faf2\"")
           == nodes(function));
    ASSERT(function->flags == flags);
    FREE_STRING(dot);
    for (function_summary_t *s = set->head; s; s = s->next)
        reset_function_summary(s);
    classify_node_properties(root, NULL);
    ASSERT(!(function->flags & (NODE_FLAG_PURE | NODE_FLAG_C_COMPATIBLE)));
    dot = generate_graph_dot(function);
    ASSERT(!wcsstr(dot.data, L"C view"));
    ASSERT(occurrences(dot.data, L"color=forestgreen") < nodes(function));
    FREE_STRING(dot);
    string_value_t after = analysis_collector_to_text(collector);
    ASSERT(!wcscmp(before.data, after.data));
    FREE_STRING(before);
    FREE_STRING(after);
    options->optimization_level = OPTIMIZATION_NONE;
    ASSERT(!analyze(root, &memory, options, NULL));
    dot = generate_graph_dot(root);
    ASSERT(!wcsstr(dot.data, L"C view") && !wcsstr(dot.data, L"color=forestgreen"));
    FREE_STRING(dot);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_specialization_graph_boundaries() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"const f=func(n){return n+1;};f(1);f(\"s\");\n"
                      L"const unused=func(n){return n;};\n"
                      L"const writer=func(n){print(n);return n;};writer(1);\n"
                      L"if(false){print(\"dead\");}"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = {.kind = ANALYSIS_FUNCTION_SUMMARY, .row = 1};
    const node_t *function = find_analysis_event(collector, NULL, &query)->node;
    ASSERT(node_has_flag(function, NODE_FLAG_PURE));
    ASSERT(!node_has_flag(function, NODE_FLAG_C_COMPATIBLE));
    query.row = 3;
    const node_t *writer = find_analysis_event(collector, NULL, &query)->node;
    ASSERT(!node_has_flag(writer, NODE_FLAG_PURE));
    ASSERT(!select_function_c_view(get_function_summaries(writer)));
    string_value_t dot = generate_graph_dot(root);
    ASSERT(occurrences(dot.data, L"C view") == 1);
    ASSERT(wcsstr(dot.data, L"C=unsupported"));
    ASSERT(wcsstr(dot.data, L"color=lightgray fontcolor=gray70 tooltip=\"unreachable\""));
    FREE_STRING(dot);
    /* A grey function remains grey even if a caller supplies an old positive summary. */
    ((node_t *)function)->flags |= NODE_FLAG_UNREACHABLE;
    dot = generate_graph_dot(function);
    ASSERT(wcsstr(dot.data, L"color=lightgray fontcolor=gray70 tooltip=\"unreachable\""));
    FREE_STRING(dot);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
