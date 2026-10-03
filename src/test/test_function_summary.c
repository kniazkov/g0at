/** @file test_function_summary.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Conservative defaults, isolated snapshots, and per-analysis reset.
 */
#include "test_function_summary.h"

#include "analysis/analysis.h"
#include "analysis/function_summary.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/expression.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <stdio.h>
#include <wchar.h>

bool test_function_summary_storage() {
    arena_t *arena = create_arena(16);
    string_view_t args[] = {{L"x", 1}, {L"y", 1}};
    node_t *function = create_function_object_node(arena, args, 2);
    function_summary_t *summary = create_function_summary(arena, function, 2);
    ASSERT(summary->function == function && summary->parameter_count == 2);
    ASSERT(summary->parameter_types[0]->type == LATTICE_TOP);
    ASSERT(summary->parameter_types[1]->type == LATTICE_TOP);
    ASSERT(summary->return_type->type == LATTICE_TOP);
    ASSERT(summary->status == FUNCTION_UNANALYZED);
    ASSERT(summary->effects == FUNCTION_EFFECT_UNKNOWN);
    ASSERT(summary->c_support == FUNCTION_C_UNKNOWN);
    const lattice_element_t **storage = summary->parameter_types;
    summary->parameter_types[0] = make_integer_element();
    summary->parameter_types[1] = make_real_element();
    summary->return_type = make_integer_element();
    summary->status = FUNCTION_ANALYZED;
    summary->effects = FUNCTION_EFFECT_NONE;
    summary->c_support = FUNCTION_C_SUPPORTED;
    arena_t *snapshots = create_arena(16);
    const function_summary_t *snapshot = snapshot_function_summary(snapshots, summary);
    ASSERT(snapshot != summary && snapshot->parameter_types != storage);
    reset_function_summary(summary);
    ASSERT(summary->parameter_types == storage && summary->function == function);
    ASSERT(summary->parameter_types[0]->type == LATTICE_INTEGER);
    ASSERT(summary->return_type->type == LATTICE_TOP);
    ASSERT(summary->effects == FUNCTION_EFFECT_UNKNOWN && summary->status == FUNCTION_UNANALYZED);
    ASSERT(summary->c_support == FUNCTION_C_UNKNOWN);
    ASSERT(snapshot->parameter_types[0]->type == LATTICE_INTEGER);
    ASSERT(snapshot->parameter_types[1]->type == LATTICE_REAL);
    ASSERT(snapshot->return_type->type == LATTICE_INTEGER);
    ASSERT(snapshot->effects == FUNCTION_EFFECT_NONE && snapshot->status == FUNCTION_ANALYZED);
    ASSERT(snapshot->c_support == FUNCTION_C_SUPPORTED);
    node_t *empty = create_function_object_node(arena, NULL, 0);
    function_summary_t *other = create_function_summary(arena, empty, 0);
    ASSERT(other != summary && other->function == empty);
    ASSERT(!other->parameter_count && !other->parameter_types);
    ASSERT(!snapshot_function_summary(snapshots, other)->parameter_types);
    destroy_arena(snapshots);
    destroy_arena(arena);
    return true;
}

bool test_function_summary_states() {
    arena_t *arena = create_arena(16);
    node_t *function = create_function_object_node(arena, NULL, 0);
    function_summary_t *summary = create_function_summary(arena, function, 0);
    const function_analysis_status_t statuses[] = {FUNCTION_UNANALYZED,
                                                   FUNCTION_ANALYZING,
                                                   FUNCTION_ANALYZED,
                                                   FUNCTION_INCONCLUSIVE};
    const wchar_t *names[] = {L"unanalyzed", L"analyzing", L"analyzed", L"inconclusive"};
    for (size_t i = 0; i < sizeof(statuses) / sizeof(*statuses); i++) {
        summary->status = statuses[i];
        string_value_t text = function_summary_to_string(summary);
        ASSERT(!wcsncmp(text.data, names[i], wcslen(names[i])));
        ASSERT(wcsstr(text.data, L"effects=unknown c=unknown"));
        FREE_STRING(text);
    }
    summary->effects = FUNCTION_EFFECT_INPUT | FUNCTION_EFFECT_OUTPUT
                       | FUNCTION_EFFECT_EXTERNAL_READ | FUNCTION_EFFECT_EXTERNAL_WRITE;
    summary->c_support = FUNCTION_C_UNSUPPORTED;
    string_value_t text = function_summary_to_string(summary);
    ASSERT(wcsstr(text.data, L"effects=input|output|external-read|external-write c=unsupported"));
    FREE_STRING(text);
    summary->effects = FUNCTION_EFFECT_NONE;
    summary->c_support = FUNCTION_C_SUPPORTED;
    text = function_summary_to_string(summary);
    ASSERT(wcsstr(text.data, L"effects=none c=supported"));
    FREE_STRING(text);
    destroy_arena(arena);
    return true;
}

bool test_function_summary_events() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"const f = func(n) { return func() { return n; }; }; var g = f(10); g();"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = {.kind = ANALYSIS_FUNCTION_SUMMARY};
    const analysis_event_t *first = find_analysis_event(collector, NULL, &query);
    ASSERT(first && first->function_summary->status == FUNCTION_ANALYZED);
    const analysis_event_t *second = find_analysis_event(collector, first, &query);
    ASSERT(second && !find_analysis_event(collector, second, &query));
    ASSERT(first->function_summary->function != second->function_summary->function);
    ASSERT(first->function_summary->parameter_count == 0);
    ASSERT(second->function_summary->parameter_count == 1);
    ASSERT(first->row == 1 && first->column > 0 && first->file_name);
    ASSERT(!first->value && !first->declarator);
    query.node = second->node;
    ASSERT(find_last_analysis_event(collector, &query) == second);
    string_value_t before = analysis_collector_to_text(collector);
    ASSERT(wcsstr(before.data, L": function-summary analyzed ("));
    ASSERT(wcsstr(before.data, L"effects=unknown c=unknown"));
    function_summary_set_t *set = get_function_summaries(second->node);
    function_summary_t *live = set->head;
    live->parameter_types[0] = make_real_element();
    live->return_type = make_integer_element();
    live->effects = FUNCTION_EFFECT_NONE;
    live->status = FUNCTION_ANALYZED;
    live->c_support = FUNCTION_C_SUPPORTED;
    ASSERT(second->function_summary->parameter_types[0]->type == LATTICE_INTEGER);
    string_value_t after = analysis_collector_to_text(collector);
    ASSERT(!wcscmp(before.data, after.data));
    FREE_STRING(after);
    ASSERT(!add_function_summary_event(NULL, live));
    options->optimization_level = OPTIMIZATION_NONE;
    ASSERT(!analyze(root, &memory, options, NULL));
    ASSERT(set == get_function_summaries(second->node));
    ASSERT(!set->head && !set->tail);
    ASSERT(second->function_summary->parameter_types[0]->type == LATTICE_INTEGER);
    after = analysis_collector_to_text(collector);
    ASSERT(!wcscmp(before.data, after.data));
    FREE_STRING(before);
    FREE_STRING(after);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
