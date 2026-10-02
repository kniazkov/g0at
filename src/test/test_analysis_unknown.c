/**
 * @file test_analysis_unknown.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Unknown expression values and valueless AST nodes.
 */
#include "analysis/abstract_state.h"
#include "analysis/analysis.h"
#include "analysis/lattice.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/binary_operation.h"
#include "graph/declarations.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "test_analysis.h"
#include "test_macro.h"

#include <stdio.h>

bool test_unknown_expression_values() {
    arena_t *arena = create_arena(8);
    abstract_state_t *state = create_abstract_state(arena);
    analysis_collector_t *collector = create_analysis_collector(arena);
    declarator_t sentinel = {.name = {L"sentinel", 8}};
    const lattice_element_t *saved = make_integer_constant_element(arena, 17);
    set_in_abstract_state(state, &sentinel, saved);
    state->collector = collector;
    state->control_flow = FLOW_NORMAL;
    const lattice_element_t *returned = make_null_element();
    state->return_value = &returned;
    expression_t *operands[] = {(expression_t *)create_integer_node(arena, 0),
                                (expression_t *)create_integer_node(arena, INT64_MAX),
                                (expression_t *)create_integer_node(arena, INT64_MIN),
                                (expression_t *)create_real_number_node(arena, 1.5),
                                (expression_t *)create_null_node(arena),
                                (expression_t *)create_true_node(arena),
                                (expression_t *)create_static_string_node(arena, L"text", 4)};
    expression_t *(*factories[])(arena_t *, expression_t *, expression_t *) = {create_less_node,
                                                                               create_greater_node};
    for (size_t op = 0; op < sizeof(factories) / sizeof(*factories); op++) {
        for (size_t i = 0; i < sizeof(operands) / sizeof(*operands); i++) {
            for (size_t j = 0; j < sizeof(operands) / sizeof(*operands); j++) {
                expression_t *expr = factories[op](arena, operands[i], operands[j]);
                ASSERT(calculate_expression(expr, state, arena)->type == LATTICE_TOP);
            }
        }
    }
    for (size_t i = 0; i < sizeof(operands) / sizeof(*operands); i++) {
        node_t *expr = create_parenthesized_expression_node(arena);
        fill_parenthesized_expression(expr, operands[i]);
        ASSERT(calculate_node(expr, state, arena)->type
               == calculate_expression(operands[i], state, arena)->type);
    }
    ASSERT(get_from_abstract_state(state, &sentinel) == saved);
    ASSERT(state->control_flow == FLOW_NORMAL && state->return_value == &returned);
    ASSERT(returned == make_null_element() && collector->count == 0);
    destroy_abstract_state(state);
    destroy_arena(arena);
    return true;
}

bool test_unknown_values_in_analysis() {
    const wchar_t *expressions[] = {L"1 < 2", L"2 > 1", L"(2 + 3) < (4 - 1)"};
    for (size_t i = 0; i < sizeof(expressions) / sizeof(*expressions); i++) {
        arena_t *arena = create_arena(8);
        parser_memory_t memory = {arena, arena, arena, arena};
        string_value_t source =
            format_string(L"var x = 0;\nx = %s;\nvar y = x;\nconst z = 1 + x;\n", expressions[i]);
        node_t *root = parse_analysis_test_program(&memory, source);
        ASSERT(root);
        options_t *options = create_options();
        analysis_collector_t *collector = create_analysis_collector(arena);
        ASSERT(!analyze(root, &memory, options, collector));
        analysis_event_query_t query = {.kind = ANALYSIS_VALUE_WRITE, .row = 2};
        const analysis_event_t *write = find_analysis_event(collector, NULL, &query);
        ASSERT(write && write->value->type == LATTICE_TOP);
        query = (analysis_event_query_t){.kind = ANALYSIS_DECLARATION_SUMMARY,
                                         .declarator = write->declarator};
        const analysis_event_t *summary = find_last_analysis_event(collector, &query);
        ASSERT(summary && summary->value->type == LATTICE_TOP);
        ASSERT(summary->declarator->abstract_value == summary->value);
        query = (analysis_event_query_t){.kind = ANALYSIS_DECLARATION_SUMMARY, .row = 3};
        summary = find_last_analysis_event(collector, &query);
        ASSERT(summary && summary->value->type == LATTICE_TOP);
        query.row = 4;
        summary = find_last_analysis_event(collector, &query);
        ASSERT(summary && summary->value->type == LATTICE_NUMERIC);
        query = (analysis_event_query_t){.kind = ANALYSIS_VALUE_WRITE, .row = 1};
        const analysis_event_t *initial = find_analysis_event(collector, NULL, &query);
        ASSERT(initial && initial->value->type == LATTICE_INTEGER_CONSTANT);
        ASSERT(((const integer_constant_element_t *)initial->value)->value == 0);
        destroy_options(options);
        destroy_arena(arena);
        FREE_STRING(source);
    }
    return true;
}

static bool
check_valueless_nodes(node_t *node, abstract_state_t *state, arena_t *arena, bool *seen) {
    switch (node->vtbl->type) {
        case NODE_ROOT:
        case NODE_ARGUMENT:
        case NODE_ARGUMENT_LIST:
        case NODE_FUNCTION_BODY:
        case NODE_VARIABLE_DECLARATOR:
        case NODE_CONSTANT_DECLARATOR:
        case NODE_VARIABLE_DECLARATION:
        case NODE_CONSTANT_DECLARATION:
        case NODE_STATEMENT_EXPRESSION:
        case NODE_RETURN:
        case NODE_IF_ELSE:
            ASSERT(calculate_node(node, state, arena)->type == LATTICE_BOTTOM);
            seen[node->vtbl->type] = true;
            break;
        default:
            break;
    }
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        ASSERT(check_valueless_nodes(get_node_child(node, i), state, arena, seen));
    }
    return true;
}

bool test_valueless_nodes_and_known_values() {
    arena_t *arena = create_arena(8);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(
            L"var x = 1; const f = func(a) { if (a) { return a; } else { return 0; } }; x = 2;"));
    ASSERT(root);
    abstract_state_t *state = create_abstract_state(arena);
    state->collector = create_analysis_collector(arena);
    bool seen[NODE_DO_WHILE + 1] = {0};
    ASSERT(check_valueless_nodes(root, state, arena, seen));
    node_type_t types[] = {NODE_ROOT,
                           NODE_ARGUMENT,
                           NODE_ARGUMENT_LIST,
                           NODE_FUNCTION_BODY,
                           NODE_VARIABLE_DECLARATOR,
                           NODE_CONSTANT_DECLARATOR,
                           NODE_VARIABLE_DECLARATION,
                           NODE_CONSTANT_DECLARATION,
                           NODE_STATEMENT_EXPRESSION,
                           NODE_RETURN,
                           NODE_IF_ELSE};
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++) {
        ASSERT(seen[types[i]]);
    }
    ASSERT(state->collector->count == 0 && state->control_flow == FLOW_NORMAL);
    expression_t *one = (expression_t *)create_integer_node(arena, 1);
    expression_t *two = (expression_t *)create_integer_node(arena, 2);
    const lattice_element_t *sum =
        calculate_expression(create_addition_node(arena, one, two), state, arena);
    ASSERT(sum->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(((const integer_constant_element_t *)sum)->value == 3);
    expression_t *invalid =
        create_addition_node(arena, (expression_t *)create_true_node(arena), one);
    ASSERT(calculate_expression(invalid, state, arena)->type == LATTICE_BOTTOM);
    ASSERT(calculate_node(create_null_node(arena), state, arena)->type == LATTICE_NULL);
    ASSERT(lattice_join(arena, sum, make_bottom_element()) == sum);
    ASSERT(lattice_join(arena, sum, make_top_element())->type == LATTICE_TOP);
    ASSERT(lattice_meet(arena, sum, make_top_element()) == sum);
    ASSERT(lattice_meet(arena, sum, make_bottom_element())->type == LATTICE_BOTTOM);
    destroy_abstract_state(state);
    destroy_arena(arena);
    return true;
}
