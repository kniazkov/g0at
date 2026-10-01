/**
 * @file test_abstract_state.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent current values, summaries, and joined states.
 */
#include <stdio.h>

#include "test_abstract_state.h"
#include "test_macro.h"
#include "analysis_test_support.h"
#include "analysis/analysis.h"
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "graph/declarations.h"
#include "cli/options.h"

static bool integer_is(const lattice_element_t *value, int64_t expected) {
    return value && value->type == LATTICE_INTEGER_CONSTANT &&
        ((const integer_constant_element_t *)value)->value == expected;
}

static bool range_is(const lattice_element_t *value, int64_t min, int64_t max) {
    if (min == max) {
        return integer_is(value, min);
    }
    return value && value->type == LATTICE_INTEGER_RANGE &&
        ((const integer_range_element_t *)value)->min == min &&
        ((const integer_range_element_t *)value)->max == max;
}

static bool summary_is(abstract_state_t *state, declarator_t *decl, int64_t min, int64_t max) {
    flush_abstract_state(state);
    return range_is(decl->abstract_value, min, max);
}

bool test_abstract_state_clone_lifetimes() {
    /* All 24 destruction orders, including destruction before the first shared write. */
    for (int a = 0; a < 4; a++) {
        for (int b = 0; b < 4; b++) {
            if (b == a) continue;
            for (int c = 0; c < 4; c++) {
                if (c == a || c == b) continue;
                int order[] = { a, b, c, 6 - a - b - c };
                arena_t *arena = create_arena(8);
                declarator_t decl = { .name = { L"x", 1 } };
                abstract_state_t *states[4];
                states[0] = create_abstract_state(arena);
                ASSERT(!set_in_abstract_state(states[0], &decl,
                    make_integer_constant_element(arena, 0)));
                states[1] = clone_abstract_state(states[0]);
                states[2] = clone_abstract_state(states[0]);
                states[3] = clone_abstract_state(states[1]);
                int64_t expected[4] = {0};
                for (int step = 0; step < 4; step++) {
                    destroy_abstract_state(states[order[step]]);
                    states[order[step]] = NULL;
                    for (int i = 0; i < 4; i++) {
                        if (!states[i]) continue;
                        ASSERT(integer_is(get_from_abstract_state(states[i], &decl), expected[i]));
                        ASSERT(summary_is(states[i], &decl, 0, expected[i]));
                        const lattice_element_t *old = set_in_abstract_state(states[i], &decl,
                            make_integer_constant_element(arena, 10 * (step + 1) + i));
                        ASSERT(integer_is(old, expected[i]));
                        expected[i] = 10 * (step + 1) + i;
                        for (int j = 0; j < 4; j++) {
                            if (states[j]) {
                                ASSERT(integer_is(get_from_abstract_state(states[j], &decl), expected[j]));
                                ASSERT(summary_is(states[j], &decl, 0, expected[j]));
                            }
                        }
                    }
                }
                destroy_arena(arena);
            }
        }
    }
    return true;
}

bool test_abstract_state_many_declarations() {
    arena_t *arena = create_arena(8);
    declarator_t declarations[64] = {0};
    declarator_t extra = {0};
    abstract_state_t *original = create_abstract_state(arena);
    for (int i = 0; i < 64; i++) {
        ASSERT(!set_in_abstract_state(original, &declarations[i],
            make_integer_constant_element(arena, i)));
    }
    abstract_state_t *copy = clone_abstract_state(original);
    abstract_state_t *nested = clone_abstract_state(copy);
    for (int i = 63; i >= 0; i--) {
        abstract_state_t *target = i % 2 ? original : copy;
        ASSERT(integer_is(set_in_abstract_state(target, &declarations[i],
            make_integer_constant_element(arena, i + 100)), i));
    }
    ASSERT(!set_in_abstract_state(copy, &extra, make_null_element()));
    ASSERT(abstract_state_contains(copy, &extra));
    ASSERT(!abstract_state_contains(original, &extra) && !abstract_state_contains(nested, &extra));
    ASSERT(!get_from_abstract_state(original, &extra));
    for (int i = 0; i < 64; i++) {
        ASSERT(integer_is(get_from_abstract_state(original, &declarations[i]), i + (i % 2 ? 100 : 0)));
        ASSERT(integer_is(get_from_abstract_state(copy, &declarations[i]), i + (i % 2 ? 0 : 100)));
        ASSERT(integer_is(get_from_abstract_state(nested, &declarations[i]), i));
        ASSERT(summary_is(original, &declarations[i], i, i + (i % 2 ? 100 : 0)));
        ASSERT(summary_is(copy, &declarations[i], i, i + (i % 2 ? 0 : 100)));
        ASSERT(summary_is(nested, &declarations[i], i, i));
    }
    destroy_abstract_state(original);
    destroy_abstract_state(copy);
    for (int i = 0; i < 64; i++) {
        ASSERT(integer_is(set_in_abstract_state(nested, &declarations[i],
            make_integer_constant_element(arena, -1)), i));
        ASSERT(summary_is(nested, &declarations[i], -1, i));
    }
    destroy_abstract_state(nested);
    destroy_arena(arena);
    return true;
}

bool test_abstract_state_join_isolation() {
    arena_t *arena = create_arena(8);
    declarator_t decl = { .name = { L"x", 1 } };
    abstract_state_t *base = create_abstract_state(arena);
    base->collector = create_analysis_collector(arena);
    set_in_abstract_state(base, &decl, make_integer_constant_element(arena, 0));
    abstract_state_t *left = clone_abstract_state(base);
    abstract_state_t *right = clone_abstract_state(base);
    set_in_abstract_state(left, &decl, make_integer_constant_element(arena, 1));
    set_in_abstract_state(left, &decl, make_integer_constant_element(arena, 5));
    set_in_abstract_state(right, &decl, make_integer_constant_element(arena, 2));
    set_in_abstract_state(right, &decl, make_integer_constant_element(arena, 4));
    abstract_state_t *joined = join_abstract_states(left, right);
    abstract_state_t *reversed = join_abstract_states(right, left);
    ASSERT(range_is(get_from_abstract_state(joined, &decl), 4, 5));
    ASSERT(range_is(get_from_abstract_state(reversed, &decl), 4, 5));
    ASSERT(summary_is(joined, &decl, 0, 5) && summary_is(reversed, &decl, 0, 5));
    ASSERT(integer_is(get_from_abstract_state(left, &decl), 5));
    ASSERT(integer_is(get_from_abstract_state(right, &decl), 4));
    ASSERT(integer_is(get_from_abstract_state(base, &decl), 0));
    ASSERT(summary_is(left, &decl, 0, 5) && summary_is(right, &decl, 0, 4));
    ASSERT(summary_is(base, &decl, 0, 0));
    collect_joined_abstract_state(joined, &decl.base);
    analysis_event_query_t query = { .kind = ANALYSIS_STATE_JOIN, .declarator = &decl };
    const analysis_event_t *event = find_last_analysis_event(base->collector, &query);
    ASSERT(event && range_is(event->value, 4, 5));
    abstract_state_t *clone = clone_abstract_state(joined);
    set_in_abstract_state(joined, &decl, make_integer_constant_element(arena, 9));
    ASSERT(range_is(get_from_abstract_state(clone, &decl), 4, 5));
    ASSERT(summary_is(clone, &decl, 0, 5) && summary_is(joined, &decl, 0, 9));
    set_in_abstract_state(left, &decl, make_integer_constant_element(arena, 20));
    ASSERT(summary_is(joined, &decl, 0, 9) && summary_is(reversed, &decl, 0, 5));
    destroy_abstract_state(base);
    destroy_abstract_state(left);
    destroy_abstract_state(right);
    destroy_abstract_state(joined);
    destroy_abstract_state(reversed);
    ASSERT(range_is(set_in_abstract_state(clone, &decl,
        make_integer_constant_element(arena, 6)), 4, 5));
    ASSERT(summary_is(clone, &decl, 0, 6));
    ASSERT(range_is(event->value, 4, 5));
    destroy_abstract_state(clone);
    destroy_arena(arena);
    return true;
}

bool test_abstract_state_clone_metadata() {
    arena_t *arena = create_arena(8);
    abstract_state_t *state = create_abstract_state(arena);
    ASSERT(!clone_abstract_state(NULL));
    ASSERT(!join_abstract_states(NULL, state) && !join_abstract_states(state, NULL));
    destroy_abstract_state(NULL);
    state->collector = create_analysis_collector(arena);
    const lattice_element_t *returned = make_integer_element();
    state->return_value = &returned;
    state->control_flow = FLOW_RETURN;
    abstract_state_t *copy = clone_abstract_state(state);
    ASSERT(copy->control_flow == FLOW_RETURN);
    ASSERT(copy->collector == state->collector && copy->arena == arena);
    ASSERT(copy->return_value == &returned);
    copy->control_flow = FLOW_NORMAL;
    copy->return_value = NULL;
    ASSERT(state->control_flow == FLOW_RETURN && state->return_value == &returned);
    ASSERT(state->collector->count == 0);
    destroy_abstract_state(state);
    destroy_abstract_state(copy);
    destroy_arena(arena);
    return true;
}

bool test_abstract_state_branch_program() {
    arena_t *arena = create_arena(8);
    parser_memory_t memory = { arena, arena, arena, arena };
    node_t *root = parse_analysis_test_program(&memory, STATIC_STRING(
        L"const choose = func() { return true; };\n"
        L"var x = 0;\n"
        L"if (choose()) { x = 1; } else { x = x + 2; }\n"
        L"var y = x;\n"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = { .kind = ANALYSIS_STATE_JOIN, .row = 3 };
    const analysis_event_t *event = find_analysis_event(collector, NULL, &query);
    while (event && !range_is(event->value, 1, 2)) {
        event = find_analysis_event(collector, event, &query);
    }
    ASSERT(event && event->declarator->name.length == 1 && event->declarator->name.data[0] == L'x');
    query = (analysis_event_query_t){ .kind = ANALYSIS_DECLARATION_SUMMARY,
        .declarator = event->declarator };
    const analysis_event_t *summary = find_last_analysis_event(collector, &query);
    ASSERT(summary && range_is(summary->value, 0, 2));
    query = (analysis_event_query_t){ .kind = ANALYSIS_DECLARATION_SUMMARY, .row = 4 };
    summary = find_last_analysis_event(collector, &query);
    ASSERT(summary && range_is(summary->value, 1, 2));
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
