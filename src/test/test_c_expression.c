/** @file test_c_expression.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Numeric edges, virtual dispatch and per-signature proof lifetime.
 */
#include "test_c_expression.h"

#include "analysis/analysis.h"
#include "analysis/c_expression.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/binary_operation.h"
#include "graph/common_methods.h"
#include "graph/expression.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <math.h>
#include <stdio.h>
#include <wchar.h>

bool test_c_expression_edges() {
    arena_t *arena = create_arena(16);
    abstract_state_t *state = create_abstract_state(arena);
    c_expression_context_t context = {.arena = arena};
    state->c_expressions = &context;
    const double reals[] = {NAN, INFINITY, -INFINITY, -0.0, 0.0, 0x1p63};
    const int64_t integers[] = {INT64_MIN, INT64_MAX, INT64_C(9007199254740993), -1, 0};
    expression_t *(*operations[])(arena_t *, expression_t *, expression_t *) = {
        create_addition_node,
        create_subtraction_node,
        create_multiplication_node,
        create_less_node,
        create_less_or_equal_node,
        create_greater_node,
        create_greater_or_equal_node,
        create_equal_node,
        create_not_equal_node};
    for (size_t op = 0; op < sizeof(operations) / sizeof(*operations); op++) {
        for (size_t i = 0; i < sizeof(integers) / sizeof(*integers); i++) {
            for (size_t r = 0; r < sizeof(reals) / sizeof(*reals); r++) {
                for (size_t reverse = 0; reverse < 2; reverse++) {
                    expression_t *a = (expression_t *)create_integer_node(arena, integers[i]);
                    expression_t *b = (expression_t *)create_real_number_node(arena, reals[r]);
                    expression_t *expr = operations[op](arena, reverse ? b : a, reverse ? a : b);
                    const lattice_element_t *value = calculate_expression(expr, state, arena);
                    ASSERT(value->type != LATTICE_BOTTOM);
                    ASSERT(c_expression_type(&context, &expr->base)
                           == (op < 3 ? C_VALUE_DOUBLE : C_VALUE_BOOL));
                    ASSERT(!expr->base.flags);
                }
            }
        }
    }
    expression_t *sum = create_addition_node(arena,
                                             (expression_t *)create_integer_node(arena, INT64_MAX),
                                             (expression_t *)create_integer_node(arena, 1));
    const lattice_element_t *value = calculate_expression(sum, state, arena);
    ASSERT(((const integer_constant_element_t *)value)->value == INT64_MAX);
    ASSERT(c_expression_type(&context, &sum->base) == C_VALUE_INT64);
    node_vtbl_t overridden = *sum->base.vtbl;
    overridden.can_generate_c_code = cannot_generate_c_code;
    sum->base.vtbl = &overridden;
    record_c_expression(&context, &sum->base, value);
    ASSERT(c_expression_type(&context, &sum->base) == C_VALUE_UNKNOWN);
    overridden.can_generate_c_code = numeric_literal_c_code;
    record_c_expression(&context, &sum->base, value);
    ASSERT(c_expression_type(&context, &sum->base) == C_VALUE_UNKNOWN);
    overridden.can_generate_c_code = NULL;
    record_c_expression(&context, &sum->base, value);
    ASSERT(c_expression_type(&context, &sum->base) == C_VALUE_UNKNOWN);
    destroy_abstract_state(state);
    destroy_arena(arena);
    return true;
}

bool test_c_expression_isolation() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"const f=func(n){return n+1;};\nf(1);\nf(1.5);"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = {.kind = ANALYSIS_C_EXPRESSION};
    const analysis_event_t *integer = NULL, *real = NULL;
    for (const analysis_event_t *event = find_analysis_event(collector, NULL, &query); event;
         event = find_analysis_event(collector, event, &query)) {
        if (event->node->vtbl->type != NODE_ADDITION)
            continue;
        if (event->function_summary->parameter_types[0]->type == LATTICE_INTEGER)
            integer = event;
        else
            real = event;
    }
    ASSERT(integer && real && integer->node == real->node);
    ASSERT(integer->c_expression->type == C_VALUE_INT64);
    ASSERT(real->c_expression->type == C_VALUE_DOUBLE);
    function_summary_set_t *set = get_function_summaries(integer->function_summary->function);
    ASSERT(set->head->c_expressions != integer->function_summary->c_expressions);
    uint32_t flags = integer->node->flags;
    string_value_t before = analysis_collector_to_text(collector);
    analyze_function_c_expressions(root);
    analyze_function_c_expressions(root);
    reset_function_summary(set->head);
    ASSERT(!set->head->c_expressions);
    string_value_t after = analysis_collector_to_text(collector);
    ASSERT(!wcscmp(before.data, after.data));
    ASSERT(integer->node->flags == flags);
    ASSERT(integer->c_expression->type == C_VALUE_INT64);
    ASSERT(real->c_expression->type == C_VALUE_DOUBLE);
    FREE_STRING(before);
    FREE_STRING(after);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
