/**
 * @file test_node_properties.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Flag snapshots, filtering, reset, and optional recording.
 */
#include "test_node_properties.h"

#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <stdio.h>
#include <wchar.h>

static size_t count_nodes(const node_t *node) {
    size_t count = 1;
    for (size_t i = 0; i < get_node_child_count(node); i++)
        count += count_nodes(get_node_child(node, i));
    return count;
}

bool test_node_property_events() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(&memory, STATIC_STRING(L"var x = 1; +x;"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = {.kind = ANALYSIS_NODE_FLAGS};
    size_t count = 0;
    for (const analysis_event_t *event = find_analysis_event(collector, NULL, &query); event;
         event = find_analysis_event(collector, event, &query)) {
        ASSERT(event->flags == event->node->flags && !event->value && !event->declarator);
        count++;
    }
    ASSERT(count == count_nodes(root));
    query.flags_mask = NODE_FLAG_C_COMPATIBLE;
    query.flags = NODE_FLAG_C_COMPATIBLE;
    const analysis_event_t *numeric = find_analysis_event(collector, NULL, &query);
    ASSERT(numeric && numeric->node->vtbl->type == NODE_INTEGER);
    query.flags_mask |= NODE_FLAG_UNREACHABLE;
    ASSERT(find_analysis_event(collector, NULL, &query) == numeric);
    query.flags = 0;
    ASSERT(find_analysis_event(collector, NULL, &query)->node->vtbl->type != NODE_INTEGER);
    query.flags_mask = UINT32_MAX;
    query.node = root;
    ASSERT(find_last_analysis_event(collector, &query)->flags == 0);
    query.kind = ANALYSIS_VALUE_WRITE;
    ASSERT(!find_analysis_event(collector, NULL, &query));
    /* Later analysis must not change the earlier event's meaning or rendering. */
    string_value_t before = analysis_collector_to_text(collector);
    ASSERT(wcsstr(before.data, L"test.goat, 1.9: flags integer = 6 pure c-compatible"));
    options->optimization_level = OPTIMIZATION_NONE;
    ASSERT(!analyze(root, &memory, options, NULL));
    ASSERT(numeric->node->flags == 0 && numeric->flags == 6);
    string_value_t after = analysis_collector_to_text(collector);
    ASSERT(!wcscmp(before.data, after.data));
    FREE_STRING(before);
    FREE_STRING(after);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

static bool no_proofs(const node_t *node) {
    if (node->flags & (NODE_FLAG_UNREACHABLE | NODE_FLAG_PURE | NODE_FLAG_C_COMPATIBLE))
        return false;
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        if (!no_proofs(get_node_child(node, i)))
            return false;
    }
    return true;
}

bool test_node_property_reset() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(&memory, STATIC_STRING(L"if (false) { 1; } 2;"));
    ASSERT(root);
    options_t *options = create_options();
    root->flags = UINT32_C(1) << 31; /* Reserved bits belong to other passes. */
    analysis_collector_t *first = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, first));
    ASSERT(root->flags & (UINT32_C(1) << 31));
    analysis_collector_t *second = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, second));
    string_value_t a = analysis_collector_to_text(first);
    string_value_t b = analysis_collector_to_text(second);
    ASSERT(!wcscmp(a.data, b.data));
    FREE_STRING(a);
    FREE_STRING(b);
    options->optimization_level = OPTIMIZATION_NONE;
    analysis_collector_t *disabled = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, disabled));
    ASSERT(!disabled->count && no_proofs(root));
    ASSERT(root->flags == (UINT32_C(1) << 31));
    options->optimization_level = OPTIMIZATION_ALL;
    ASSERT(!analyze(root, &memory, options, NULL));
    ASSERT(node_has_flag(root, NODE_FLAG_PURE));
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
