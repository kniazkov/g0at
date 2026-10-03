/**
 * @file test_node_properties.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Flag snapshots, filtering, reset, and optional recording.
 */
#include "test_node_properties.h"

#include "analysis/analysis.h"
#include "analysis/properties.h"
#include "analysis/reachability.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/common_methods.h"
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

static size_t reachability_calls;

/** @brief Test override: the driver must dispatch through the table and accept a replaced state. */
static const lattice_element_t *
custom_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    reachability_calls++;
    abstract_state_t *replacement = clone_abstract_state(*state);
    destroy_abstract_state(*state);
    *state = replacement;
    return make_integer_constant_element(replacement->arena, 19);
}

static bool custom_c_subset(const node_t *node,
                            const lattice_element_t *value,
                            const c_expression_context_t *context) {
    return value && value->type == LATTICE_INTEGER_CONSTANT
           && ((const integer_constant_element_t *)value)->value == 19;
}

bool test_node_virtual_analysis() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(&memory, STATIC_STRING(L"7;"));
    ASSERT(root);
    node_t *literal = get_node_child(get_node_child(root, 0), 0);
    node_vtbl_t overridden = *literal->vtbl;
    overridden.analyze_reachability = custom_reachability;
    overridden.is_pure = not_pure;
    overridden.can_generate_c_code = custom_c_subset;
    literal->vtbl = &overridden;
    reachability_calls = 0;
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    ASSERT(reachability_calls == 1);
    /* Type is still INTEGER: a hard-coded type switch would produce the wrong flags. */
    ASSERT(literal->flags == NODE_FLAG_C_COMPATIBLE);
    ASSERT(!can_generate_c_code_from_node(literal, NULL));
    analysis_event_query_t query = {.kind = ANALYSIS_NODE_FLAGS, .node = literal};
    const analysis_event_t *event = find_last_analysis_event(collector, &query);
    ASSERT(event && event->flags == NODE_FLAG_C_COMPATIBLE);
    abstract_state_t *state = create_abstract_state(arena);
    state->control_flow = FLOW_RETURN;
    ASSERT(visit_reachable_node(literal, &state, collector)->type == LATTICE_BOTTOM);
    ASSERT(reachability_calls == 1);
    classify_node_properties(literal, collector);
    ASSERT(literal->flags == NODE_FLAG_UNREACHABLE);
    ASSERT(event->flags == NODE_FLAG_C_COMPATIBLE);
    destroy_abstract_state(state);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
