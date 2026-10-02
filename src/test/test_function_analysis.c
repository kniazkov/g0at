/** @file test_function_analysis.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Known callables, branch effects, and evaluation limits.
 */
#include "test_function_analysis.h"

#include "analysis/abstract_state.h"
#include "analysis/analysis.h"
#include "analysis/comparison.h"
#include "analysis/function_call.h"
#include "analysis/lattice.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/declarations.h"
#include "graph/expression.h"
#include "test_macro.h"

#include <stdio.h>

bool test_function_lattice() {
    arena_t *arena = create_arena(8);
    node_t a = {0}, b = {0};
    const lattice_element_t *first = make_known_function_element(arena, &a, NULL);
    const lattice_element_t *same = make_known_function_element(arena, &a, NULL);
    const lattice_element_t *other = make_known_function_element(arena, &b, NULL);
    ASSERT(lattice_truth(first) == ABSTRACT_TRUE);
    ASSERT(lattice_join(arena, first, same) == first);
    ASSERT(lattice_join(arena, first, make_bottom_element()) == first);
    ASSERT(lattice_join(arena, make_bottom_element(), first) == first);
    ASSERT(lattice_join(arena, first, other)->type == LATTICE_FUNCTION);
    ASSERT(lattice_join(arena, first, make_function_element())->type == LATTICE_FUNCTION);
    ASSERT(lattice_join(arena, make_function_element(), first)->type == LATTICE_FUNCTION);
    ASSERT(lattice_meet(arena, first, same) == first);
    ASSERT(lattice_meet(arena, first, other)->type == LATTICE_BOTTOM);
    ASSERT(lattice_meet(arena, first, make_function_element()) == first);
    ASSERT(lattice_meet(arena, make_function_element(), first) == first);
    ASSERT(lattice_meet(arena, first, make_top_element()) == first);
    ASSERT(lattice_meet(arena, make_not_null_element(), first) == first);
    ASSERT(lattice_compare(first, same, COMPARE_EQUAL)->type == LATTICE_BOOLEAN);
    ASSERT(lattice_compare(first, make_integer_element(), COMPARE_EQUAL)->type == LATTICE_FALSE);
    ASSERT(lattice_compare(first, same, COMPARE_LESS)->type == LATTICE_BOTTOM);
    destroy_arena(arena);
    return true;
}

bool test_function_call_budget() {
    arena_t *arena = create_arena(8);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(&memory, STATIC_STRING(L"var f=func(){return 7;};"));
    ASSERT(root);
    options_t *options = create_options();
    options->optimization_level = OPTIMIZATION_NONE;
    ASSERT(!analyze(root, &memory, options, NULL));
    node_t *declaration = get_node_child(get_node_child(root, 0), 0);
    node_t *function = get_node_child(declaration, 0);
    abstract_state_t *state = create_abstract_state(arena);
    const lattice_element_t *value = calculate_node(function, state, arena);
    *state->call_budget = 1;
    abstract_state_t *clone = clone_abstract_state(state);
    const lattice_element_t *result = interpret_function_call(value, NULL, 0, clone);
    ASSERT(result->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(((const integer_constant_element_t *)result)->value == 7);
    ASSERT(*state->call_budget == 0);
    result = interpret_function_call(value, NULL, 0, state);
    ASSERT(result->type == LATTICE_TOP);
    ASSERT(state->control_flow == FLOW_NORMAL && !state->call_frame && !state->return_value);
    destroy_abstract_state(clone);
    destroy_abstract_state(state);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_function_call_state() {
    arena_t *arena = create_arena(8);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"var x=0;\nvar f=func(c){if(c){x=2;return 10;}x=4;return 20;};\n"
                      L"var a=f(pi);\nvar y=x;\nvar b=f(true);\nvar z=x;\n"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = {.kind = ANALYSIS_VALUE_WRITE, .row = 4};
    const analysis_event_t *event = find_last_analysis_event(collector, &query);
    ASSERT(event && event->value->type == LATTICE_INTEGER_RANGE);
    const integer_range_element_t *range = (const integer_range_element_t *)event->value;
    ASSERT(range->min == 2 && range->max == 4);
    query.row = 6;
    event = find_last_analysis_event(collector, &query);
    ASSERT(event && event->value->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(((const integer_constant_element_t *)event->value)->value == 2);
    query.kind = ANALYSIS_UNREACHABLE;
    query.row = 2;
    ASSERT(!find_analysis_event(collector, NULL, &query));
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
