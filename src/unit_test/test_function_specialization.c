/** @file test_function_specialization.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Type normalization, call registration, and per-run isolation.
 */
#include "test_function_specialization.h"

#include "analysis/analysis.h"
#include "analysis/function_summary.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/expression.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <stdio.h>
#include <wchar.h>

bool test_function_signature_types() {
    arena_t *arena = create_arena(16);
    string_view_t arg = {L"x", 1};
    node_t *function = create_function_object_node(arena, &arg, 1);
    function_summary_set_t *set = get_function_summaries(function);
    ASSERT(!set->head && !set->tail);
    const lattice_element_t *values[] = {
        make_integer_constant_element(arena, 10),
        make_integer_constant_element(arena, 20),
        make_integer_range_element(arena, -5, 8),
        make_integer_element(),
        make_real_constant_element(arena, 1.5),
        make_real_constant_element(arena, -2.0),
        make_real_element(),
        make_string_constant_element(arena, (string_view_t){L"a", 1}),
        make_string_constant_element(arena, (string_view_t){L"b", 1}),
        make_string_element(),
        make_true_element(),
        make_false_element(),
        make_boolean_element(),
        make_known_function_element(arena, function, NULL),
        make_function_element(),
        make_typed_array_element(arena, LATTICE_INTEGER),
        make_array_element(),
        make_null_element(),
        make_numeric_element(),
        make_not_null_element(),
        make_user_defined_object_element(),
        make_top_element()};
    const lattice_type_t types[] = {LATTICE_INTEGER, LATTICE_INTEGER,  LATTICE_INTEGER,
                                    LATTICE_INTEGER, LATTICE_REAL,     LATTICE_REAL,
                                    LATTICE_REAL,    LATTICE_STRING,   LATTICE_STRING,
                                    LATTICE_STRING,  LATTICE_BOOLEAN,  LATTICE_BOOLEAN,
                                    LATTICE_BOOLEAN, LATTICE_FUNCTION, LATTICE_FUNCTION,
                                    LATTICE_ARRAY,   LATTICE_ARRAY,    LATTICE_NULL,
                                    LATTICE_NUMERIC, LATTICE_NOT_NULL, LATTICE_USER_DEFINED_OBJECT,
                                    LATTICE_TOP};
    function_summary_t *previous = NULL;
    size_t unique = 0;
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); i++) {
        function_summary_t *summary = register_function_specialization(set, &values[i], 1);
        ASSERT(summary->parameter_types[0]->type == types[i]);
        ASSERT(summary->status == FUNCTION_UNANALYZED && summary->return_type->type == LATTICE_TOP);
        ASSERT(summary->effects == FUNCTION_EFFECT_UNKNOWN
               && summary->c_support == FUNCTION_C_UNKNOWN);
        if (i && types[i] == types[i - 1]) {
            ASSERT(summary == previous);
        } else {
            ASSERT(summary != previous);
            unique++;
        }
        previous = summary;
    }
    size_t count = 0;
    for (function_summary_t *s = set->head; s; s = s->next)
        count++;
    ASSERT(count == unique && set->tail == previous);
    ASSERT(!snapshot_function_summary(arena, set->head)->next);
    const lattice_element_t *bottom = make_bottom_element();
    ASSERT(!register_function_specialization(set, &bottom, 1));
    ASSERT(set->tail == previous);
    destroy_arena(arena);
    return true;
}

bool test_function_signature_arguments() {
    arena_t *arena = create_arena(16);
    string_view_t params[] = {{L"a", 1}, {L"b", 1}};
    node_t *function = create_function_object_node(arena, params, 2);
    function_summary_set_t *set = get_function_summaries(function);
    const lattice_element_t *args[] = {make_integer_element(),
                                       make_null_element(),
                                       make_real_element()};
    function_summary_t *one = register_function_specialization(set, args, 1);
    ASSERT(one == register_function_specialization(set, args, 2));
    ASSERT(one == register_function_specialization(set, args, 3));
    ASSERT(one->parameter_types[1]->type == LATTICE_NULL);
    const lattice_element_t *swapped[] = {make_null_element(), make_integer_element()};
    ASSERT(one != register_function_specialization(set, swapped, 2));
    function_summary_t *missing = register_function_specialization(set, NULL, 0);
    ASSERT(missing->parameter_types[0]->type == LATTICE_NULL);
    ASSERT(missing->parameter_types[1]->type == LATTICE_NULL);
    node_t *other = create_function_object_node(arena, params, 2);
    ASSERT(one != register_function_specialization(get_function_summaries(other), args, 2));
    function_summary_set_t *empty =
        get_function_summaries(create_function_object_node(arena, NULL, 0));
    function_summary_t *zero = register_function_specialization(empty, NULL, 0);
    ASSERT(zero == register_function_specialization(empty, args, 3));
    args[2] = make_bottom_element();
    ASSERT(!register_function_specialization(empty, args, 3));
    reset_function_summary_set(set);
    ASSERT(!set->head && !set->tail);
    ASSERT(one != register_function_specialization(set, args, 1));
    ASSERT(one->parameter_types[0]->type == LATTICE_INTEGER);
    destroy_arena(arena);
    return true;
}

bool test_function_signature_calls() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(
            L"const f=func(n){return n;};\n"
            L"f(10); f(20); f(1.5); f(\"x\"); f(true); f(false); f(); f(null); f(10, 99);\n"
            L"const unused=func(x){return x;};\n"
            L"const g=func(n){return n;}; g(10);\n"
            L"const r=func(n){return r(n);}; r(10);\n"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = {.kind = ANALYSIS_FUNCTION_SUMMARY, .row = 1};
    const lattice_type_t expected[] = {LATTICE_INTEGER,
                                       LATTICE_REAL,
                                       LATTICE_STRING,
                                       LATTICE_BOOLEAN,
                                       LATTICE_NULL};
    const analysis_event_t *event = NULL;
    for (size_t i = 0; i < sizeof(expected) / sizeof(*expected); i++) {
        event = find_analysis_event(collector, event, &query);
        ASSERT(event && event->function_summary->parameter_types[0]->type == expected[i]);
        ASSERT(event->function_summary->status == FUNCTION_ANALYZED);
        ASSERT(event->function_summary->return_type->type == expected[i]);
    }
    ASSERT(!find_analysis_event(collector, event, &query));
    query.row = 3;
    ASSERT(!find_analysis_event(collector, NULL, &query));
    query.row = 4;
    ASSERT(find_analysis_event(collector, NULL, &query)->function_summary->parameter_types[0]->type
           == LATTICE_INTEGER);
    query.row = 5;
    event = find_analysis_event(collector, NULL, &query);
    ASSERT(event && !find_analysis_event(collector, event, &query));
    ASSERT(event->function_summary->parameter_types[0]->type == LATTICE_INTEGER);
    string_value_t before = analysis_collector_to_text(collector);
    analysis_collector_t *again = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, again));
    string_value_t after = analysis_collector_to_text(collector);
    ASSERT(!wcscmp(before.data, after.data));
    FREE_STRING(after);
    query.row = 1;
    size_t count = 0;
    for (event = find_analysis_event(again, NULL, &query); event;
         event = find_analysis_event(again, event, &query))
        count++;
    ASSERT(count == 5);
    options->optimization_level = OPTIMIZATION_NONE;
    analysis_collector_t *disabled = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, disabled));
    query.row = 0;
    ASSERT(!find_analysis_event(disabled, NULL, &query));
    FREE_STRING(before);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
