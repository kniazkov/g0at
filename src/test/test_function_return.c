/** @file test_function_return.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Generic proofs must not replace concrete caller observations.
 */
#include "test_function_return.h"

#include "analysis/analysis.h"
#include "analysis/function_return.h"
#include "analysis/function_summary.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/declarations.h"
#include "graph/expression.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <stdio.h>
#include <wchar.h>

bool test_function_return_isolation() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(
            L"var x=0;\nconst f=func(n){x=n;if(n==1)return 10;return 20;};\nvar y=f(1);"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = {.kind = ANALYSIS_FUNCTION_SUMMARY};
    const analysis_event_t *event = find_analysis_event(collector, NULL, &query);
    ASSERT(event && !find_analysis_event(collector, event, &query));
    const node_t *function = event->node;
    function_summary_set_t *set = get_function_summaries(function);
    ASSERT(set->head == set->tail);
    ASSERT(set->head->status == FUNCTION_ANALYZED);
    ASSERT(set->head->return_type->type == LATTICE_INTEGER);
    ASSERT(set->head->effects == FUNCTION_EFFECT_EXTERNAL_WRITE);
    ASSERT(set->head->c_support == FUNCTION_C_UNSUPPORTED);
    query.kind = ANALYSIS_DECLARATION_SUMMARY;
    query.row = 3;
    const analysis_event_t *result = find_analysis_event(collector, NULL, &query);
    ASSERT(result && result->value->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(((const integer_constant_element_t *)result->value)->value == 10);
    const lattice_element_t *fact = result->declarator->abstract_value;
    uint32_t flags = function->flags;
    string_value_t before = analysis_collector_to_text(collector);
    analyze_function_return_types(root);
    analyze_function_return_types(root);
    string_value_t after = analysis_collector_to_text(collector);
    ASSERT(!wcscmp(before.data, after.data));
    ASSERT(result->declarator->abstract_value == fact && function->flags == flags);
    ASSERT(set->head == set->tail && set->head->return_type->type == LATTICE_INTEGER);
    ASSERT(event->function_summary->return_type->type == LATTICE_INTEGER);
    FREE_STRING(before);
    FREE_STRING(after);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
