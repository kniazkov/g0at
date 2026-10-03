/** @file test_function_effects.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Capture identity, snapshots, and conservative fallback.
 */
#include "test_function_effects.h"

#include "analysis/analysis.h"
#include "analysis/function_effects.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/common_methods.h"
#include "graph/expression.h"
#include "graph/variable.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <stdio.h>
#include <wchar.h>

bool test_function_effect_captures() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(
            L"var outer=1;\nconst f=func(n){outer=n;outer++;print(n);return input();};f(1);"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = {.kind = ANALYSIS_FUNCTION_SUMMARY};
    const analysis_event_t *event = find_analysis_event(collector, NULL, &query);
    ASSERT(event && !find_analysis_event(collector, event, &query));
    const function_summary_t *snapshot = event->function_summary;
    ASSERT(snapshot->effects
           == (FUNCTION_EFFECT_EXTERNAL_READ | FUNCTION_EFFECT_EXTERNAL_WRITE
               | FUNCTION_EFFECT_UNKNOWN));
    ASSERT(snapshot->c_support == FUNCTION_C_UNSUPPORTED);
    ASSERT(snapshot->has_calls);
    ASSERT(snapshot->direct_effects
           == (FUNCTION_EFFECT_EXTERNAL_READ | FUNCTION_EFFECT_EXTERNAL_WRITE));
    const function_capture_t *outer = snapshot->captures;
    ASSERT(outer && outer->access == (FUNCTION_CAPTURE_READ | FUNCTION_CAPTURE_WRITE));
    ASSERT(outer->name.length == 5 && !wmemcmp(outer->name.data, L"outer", 5));
    const function_capture_t *print =
        find_function_capture(snapshot, get_builtin_declarator(), (string_view_t){L"print", 5});
    const function_capture_t *input =
        find_function_capture(snapshot, get_builtin_declarator(), (string_view_t){L"input", 5});
    ASSERT(print && input && print != input);
    ASSERT(print->access == FUNCTION_CAPTURE_READ && input->access == FUNCTION_CAPTURE_READ);
    ASSERT(!input->next);
    ASSERT(!find_function_capture(snapshot, outer->declarator, (string_view_t){L"print", 5}));
    function_summary_t *live = get_function_summaries(event->node)->head;
    ASSERT(live->captures != snapshot->captures);
    string_value_t before = analysis_collector_to_text(collector);
    ASSERT(wcsstr(before.data, L"outer@1.1:read|write"));
    ASSERT(wcsstr(before.data, L"print@0.0:read,input@0.0:read"));
    live->captures->access = 0;
    reset_function_summary(live);
    ASSERT(live->direct_effects == FUNCTION_EFFECT_UNKNOWN && !live->captures && !live->has_calls);
    analyze_function_direct_effects(root);
    ASSERT(live->captures && live->captures->access == outer->access);
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

bool test_function_effect_unknown() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root =
        parse_analysis_test_program(&memory, STATIC_STRING(L"const f=func(n){return n;};f(1);"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = {.kind = ANALYSIS_FUNCTION_SUMMARY};
    const analysis_event_t *event = find_analysis_event(collector, NULL, &query);
    ASSERT(event && event->function_summary->direct_effects == FUNCTION_EFFECT_NONE);
    function_summary_t *live = get_function_summaries(event->node)->head;
    node_t *ret = get_node_child(get_node_child(event->node, 1), 0);
    node_vtbl_t *original = ret->vtbl;
    node_vtbl_t future = *original;
    future.type = (node_type_t)999;
    ret->vtbl = &future;
    analyze_function_direct_effects(root);
    ASSERT(live->direct_effects == FUNCTION_EFFECT_NONE);
    future.collect_direct_effects = unknown_direct_effects;
    analyze_function_direct_effects(root);
    ASSERT(live->direct_effects == FUNCTION_EFFECT_UNKNOWN);
    future.collect_direct_effects = NULL;
    analyze_function_direct_effects(root);
    ASSERT(live->direct_effects == FUNCTION_EFFECT_UNKNOWN);
    ret->vtbl = original;
    variable_t *variable = (variable_t *)get_node_child(ret, 0);
    declarator_t *decl = variable->declarator;
    variable->declarator = NULL;
    analyze_function_direct_effects(root);
    ASSERT(live->direct_effects == FUNCTION_EFFECT_UNKNOWN);
    variable->declarator = decl;
    analyze_function_direct_effects(root);
    ASSERT(live->direct_effects == FUNCTION_EFFECT_NONE);
    ASSERT(event->function_summary->direct_effects == FUNCTION_EFFECT_NONE);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
