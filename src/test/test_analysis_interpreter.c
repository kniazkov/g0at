/**
 * @file test_analysis_interpreter.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Analysis observations from parsed programs.
 */
#include "analysis/analysis.h"
#include "analysis/lattice.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/declarations.h"
#include "test_analysis.h"
#include "test_macro.h"

#include <stdio.h>

bool test_analysis_observations() {
    arena_t *arena = create_arena(8);
    arena_t *events = create_arena(1);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root =
        parse_analysis_test_program(&memory,
                                    STATIC_STRING(L"var x = 1;\nx = 3;\nconst y = x + 1;\n"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(events);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = {.kind = ANALYSIS_VALUE_WRITE,
                                    .row = 2,
                                    .column = 1,
                                    .file_name = "test.goat"};
    const analysis_event_t *write = find_analysis_event(collector, NULL, &query);
    ASSERT(write && write->value->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(((const integer_constant_element_t *)write->value)->value == 3);
    query = (analysis_event_query_t){.kind = ANALYSIS_DECLARATION_SUMMARY,
                                     .declarator = write->declarator};
    const analysis_event_t *summary = find_last_analysis_event(collector, &query);
    ASSERT(summary && summary->row == 1 && summary->value->type == LATTICE_INTEGER_RANGE);
    const integer_range_element_t *range = (const integer_range_element_t *)summary->value;
    ASSERT(range->min == 1 && range->max == 3);
    ASSERT(summary->declarator->abstract_value == summary->value);
    query = (analysis_event_query_t){.kind = ANALYSIS_DECLARATION_SUMMARY, .row = 3};
    summary = find_analysis_event(collector, NULL, &query);
    ASSERT(summary && summary->value->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(((const integer_constant_element_t *)summary->value)->value == 4);
    query = (analysis_event_query_t){.kind = ANALYSIS_VALUE_WRITE, .row = 1};
    const analysis_event_t *initial = find_analysis_event(collector, NULL, &query);
    ASSERT(initial && ((const integer_constant_element_t *)initial->value)->value == 1);

    node_t *plain =
        parse_analysis_test_program(&memory,
                                    STATIC_STRING(L"var x = 1;\nx = 3;\nconst y = x + 1;\n"));
    ASSERT(plain && !analyze(plain, &memory, options, NULL));
    node_t *declaration = get_node_child(plain, 2);
    declarator_t *y = (declarator_t *)get_node_child(declaration, 0);
    ASSERT(y->abstract_value->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(((const integer_constant_element_t *)y->abstract_value)->value == 4);
    destroy_options(options);
    destroy_arena(events);
    destroy_arena(arena);
    return true;
}

bool test_analysis_branch_observations() {
    arena_t *arena = create_arena(8);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"var x = 1;\nif (pi) { x = 1; } else { x = 1; }\nx = 2;\n"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = {.kind = ANALYSIS_STATE_JOIN, .row = 2};
    const analysis_event_t *joined = find_analysis_event(collector, NULL, &query);
    ASSERT(joined && joined->value->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(((const integer_constant_element_t *)joined->value)->value == 1);
    query = (analysis_event_query_t){.kind = ANALYSIS_VALUE_WRITE,
                                     .row = 2,
                                     .declarator = joined->declarator};
    ASSERT(find_analysis_event(collector, NULL, &query));
    query = (analysis_event_query_t){.kind = ANALYSIS_VALUE_WRITE,
                                     .row = 3,
                                     .declarator = joined->declarator};
    const analysis_event_t *write = find_analysis_event(collector, NULL, &query);
    ASSERT(write && ((const integer_constant_element_t *)write->value)->value == 2);
    query = (analysis_event_query_t){.kind = ANALYSIS_DECLARATION_SUMMARY,
                                     .declarator = joined->declarator};
    ASSERT(find_last_analysis_event(collector, &query));
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
