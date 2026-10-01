/**
 * @file test_analysis.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Collector queries, rendering, and interpreter integration.
 */
#include <stdio.h>
#include <string.h>

#include "test_analysis.h"
#include "test_macro.h"
#include "analysis/analysis.h"
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "graph/declarations.h"
#include "graph/binary_operation.h"
#include "cli/options.h"
#include "scanner/scanner.h"
#include "parser/parser.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

bool test_analysis_collector() {
    arena_t *arena = create_arena(1);
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!collector->head && !collector->tail && collector->count == 0);
    ASSERT(!find_analysis_event(collector, NULL, NULL));
    ASSERT(!find_last_analysis_event(collector, NULL));
    ASSERT(!add_analysis_event(NULL, ANALYSIS_VALUE_WRITE, NULL, NULL, NULL));
    ASSERT(!find_analysis_event(NULL, NULL, NULL));
    ASSERT(!find_last_analysis_event(NULL, NULL));

    full_position_t position = { .file_name = "test.goat", .row = 7, .column = 3 };
    position_range_t range = { .begin = &position };
    node_t parent = { .position = &range };
    node_t node = { .parent = &parent };
    declarator_t first = { .name = { L"x", 1 } };
    declarator_t second = { .name = { L"x", 1 } };
    const lattice_element_t *one = make_integer_constant_element(arena, 1);
    const analysis_event_t *a = add_analysis_event(collector, ANALYSIS_VALUE_WRITE,
        &node, &first, one);
    const analysis_event_t *b = add_analysis_event(collector, ANALYSIS_VALUE_WRITE,
        &node, &second, make_top_element());
    const analysis_event_t *c = add_analysis_event(collector, ANALYSIS_DECLARATION_SUMMARY,
        &node, &first, make_integer_element());
    position.row = 99;
    node.parent = NULL;
    ASSERT(a->row == 7 && a->column == 3 && a->value == one);
    ASSERT(a->next == b && b->next == c && !c->next);
    ASSERT(collector->head == a && collector->tail == c && collector->count == 3);

    char filename[] = "test.goat";
    analysis_event_query_t query = { .kind = ANALYSIS_VALUE_WRITE, .node = &node,
        .declarator = &first, .file_name = filename, .row = 7, .column = 3 };
    ASSERT(find_analysis_event(collector, NULL, &query) == a);
    ASSERT(!find_analysis_event(collector, a, &query));
    query.kind = ANALYSIS_EVENT_ANY;
    ASSERT(find_analysis_event(collector, a, &query) == c);
    ASSERT(find_last_analysis_event(collector, &query) == c);
    query.declarator = &second;
    ASSERT(find_last_analysis_event(collector, &query) == b);
    query.column = 4;
    ASSERT(!find_analysis_event(collector, NULL, &query));
    query.column = 3;
    query.row = 8;
    ASSERT(!find_analysis_event(collector, NULL, &query));
    query.row = 7;
    query.file_name = "other.goat";
    ASSERT(!find_analysis_event(collector, NULL, &query));
    query.file_name = NULL;
    query.node = &parent;
    ASSERT(!find_analysis_event(collector, NULL, &query));

    for (size_t i = 0; i < 1000; i++) {
        add_analysis_event(collector, ANALYSIS_VALUE_WRITE, NULL, &first, one);
    }
    ASSERT(a == collector->head && a->value == one && a->next == b);
    size_t count = 0;
    for (const analysis_event_t *event = collector->head; event; event = event->next) {
        ASSERT(event->sequence == ++count);
    }
    ASSERT(count == 1003 && collector->tail->sequence == count && !collector->tail->next);
    destroy_arena(arena);
    return true;
}

bool test_analysis_collector_text() {
    arena_t *arena = create_arena(1);
    analysis_collector_t *collector = create_analysis_collector(arena);
    string_value_t text = analysis_collector_to_text(collector);
    ASSERT(text.data && text.length == 0);
    FREE_STRING(text);
    text = analysis_collector_to_text(NULL);
    ASSERT(text.data && text.length == 0);
    FREE_STRING(text);
    full_position_t position = { .file_name = "тест%.goat", .row = 2, .column = 4 };
    position_range_t range = { .begin = &position };
    node_t node = { .position = &range };
    declarator_t decl = { .name = { L"число", 5 } };
    add_analysis_event(collector, ANALYSIS_VALUE_WRITE, &node, &decl,
        make_integer_constant_element(arena, 1));
    add_analysis_event(collector, ANALYSIS_STATE_JOIN, &node, &decl,
        make_integer_range_element(arena, 1, 3));
    add_analysis_event(collector, ANALYSIS_DECLARATION_SUMMARY, NULL, &decl,
        make_top_element());
    text = analysis_collector_to_text(collector);
    ASSERT(wcscmp(text.data,
        L"#1 тест%.goat, 2.4: write число = 1\n"
        L"#2 тест%.goat, 2.4: join число = [1..3]\n"
        L"#3 <unknown>, 0.0: summary число = ⊤\n") == 0);
    ASSERT(text.length == wcslen(text.data));
    FREE_STRING(text);
    destroy_arena(arena);
    return true;
}

static node_t *parse_program(parser_memory_t *memory, string_value_t source) {
    token_groups_t *groups = CALLOC(sizeof(*groups));
    scanner_t *scan = create_scanner("test.goat", source, memory, groups);
    token_list_t tokens;
    parsing_result_t result = {0};
    node_t *root = NULL;
    compilation_error_t *error = process_brackets(memory, scan, &tokens, groups);
    if (!error) {
        error = apply_reduction_rules(groups, memory, &result);
    }
    if (!error) {
        error = process_root_token_list(memory, &tokens, &root);
    }
    FREE(groups);
    return error ? NULL : root;
}

bool test_analysis_observations() {
    arena_t *arena = create_arena(8);
    arena_t *events = create_arena(1);
    parser_memory_t memory = { arena, arena, arena, arena };
    node_t *root = parse_program(&memory,
        STATIC_STRING(L"var x = 1;\nx = 3;\nconst y = x + 1;\n"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(events);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = { .kind = ANALYSIS_VALUE_WRITE, .row = 2,
        .column = 1, .file_name = "test.goat" };
    const analysis_event_t *write = find_analysis_event(collector, NULL, &query);
    ASSERT(write && write->value->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(((const integer_constant_element_t *)write->value)->value == 3);
    query = (analysis_event_query_t){ .kind = ANALYSIS_DECLARATION_SUMMARY,
        .declarator = write->declarator };
    const analysis_event_t *summary = find_last_analysis_event(collector, &query);
    ASSERT(summary && summary->row == 1 && summary->value->type == LATTICE_INTEGER_RANGE);
    const integer_range_element_t *range = (const integer_range_element_t *)summary->value;
    ASSERT(range->min == 1 && range->max == 3);
    ASSERT(summary->declarator->abstract_value == summary->value);
    query = (analysis_event_query_t){ .kind = ANALYSIS_DECLARATION_SUMMARY, .row = 3 };
    summary = find_analysis_event(collector, NULL, &query);
    ASSERT(summary && summary->value->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(((const integer_constant_element_t *)summary->value)->value == 4);
    query = (analysis_event_query_t){ .kind = ANALYSIS_VALUE_WRITE, .row = 1 };
    const analysis_event_t *initial = find_analysis_event(collector, NULL, &query);
    ASSERT(initial && ((const integer_constant_element_t *)initial->value)->value == 1);

    node_t *plain = parse_program(&memory,
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
    parser_memory_t memory = { arena, arena, arena, arena };
    node_t *root = parse_program(&memory, STATIC_STRING(
        L"var x = 1;\nif (true) { x = 1; } else { x = 1; }\nx = 2;\n"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = { .kind = ANALYSIS_STATE_JOIN, .row = 2 };
    const analysis_event_t *joined = find_analysis_event(collector, NULL, &query);
    ASSERT(joined && joined->value->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(((const integer_constant_element_t *)joined->value)->value == 1);
    query = (analysis_event_query_t){ .kind = ANALYSIS_VALUE_WRITE, .row = 2,
        .declarator = joined->declarator };
    ASSERT(find_analysis_event(collector, NULL, &query));
    query = (analysis_event_query_t){ .kind = ANALYSIS_VALUE_WRITE, .row = 3,
        .declarator = joined->declarator };
    const analysis_event_t *write = find_analysis_event(collector, NULL, &query);
    ASSERT(write && ((const integer_constant_element_t *)write->value)->value == 2);
    query = (analysis_event_query_t){ .kind = ANALYSIS_DECLARATION_SUMMARY,
        .declarator = joined->declarator };
    ASSERT(find_last_analysis_event(collector, &query));
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_analysis_options() {
    char *args[] = { "goat", "--print-analysis", "--save-analysis", "first.txt",
        "--save-analysis", "second.txt", "--save-graph", "ast.svg", "test.goat" };
    options_t *options = parse_options(9, args);
    ASSERT(options && options->print_analysis && options->analysis_output_file);
    ASSERT(strcmp(options->analysis_output_file->file_name, "second.txt") == 0);
    ASSERT(options->graph_output_file);
    ASSERT(strcmp(options->graph_output_file->file_name, "ast.svg") == 0);
    destroy_options(options);
    options = create_options();
    ASSERT(!options->print_analysis && !options->analysis_output_file);
    destroy_options(options);
    return true;
}

bool test_unknown_expression_values() {
    arena_t *arena = create_arena(8);
    abstract_state_t *state = create_abstract_state(arena);
    analysis_collector_t *collector = create_analysis_collector(arena);
    declarator_t sentinel = { .name = { L"sentinel", 8 } };
    const lattice_element_t *saved = make_integer_constant_element(arena, 17);
    set_in_abstract_state(state, &sentinel, saved);
    state->collector = collector;
    state->control_flow = FLOW_RETURN;
    const lattice_element_t *returned = make_null_element();
    state->return_value = &returned;
    expression_t *operands[] = {
        (expression_t *)create_integer_node(arena, 0),
        (expression_t *)create_integer_node(arena, INT64_MAX),
        (expression_t *)create_integer_node(arena, INT64_MIN),
        (expression_t *)create_real_number_node(arena, 1.5),
        (expression_t *)create_null_node(arena),
        (expression_t *)create_true_node(arena),
        (expression_t *)create_static_string_node(arena, L"text", 4)
    };
    expression_t *(*factories[])(arena_t *, expression_t *, expression_t *) = {
        create_subtraction_node, create_multiplication_node, create_division_node,
        create_modulo_node, create_power_node, create_less_node, create_greater_node
    };
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
        ASSERT(calculate_node(expr, state, arena)->type == LATTICE_TOP);
    }
    ASSERT(get_from_abstract_state(state, &sentinel) == saved);
    ASSERT(state->control_flow == FLOW_RETURN && state->return_value == &returned);
    ASSERT(returned == make_null_element() && collector->count == 0);
    destroy_abstract_state(state);
    destroy_arena(arena);
    return true;
}

bool test_unknown_values_in_analysis() {
    const wchar_t *expressions[] = {
        L"5 - 2", L"2 * 3", L"6 / 2", L"5 % 2", L"2 ** 3",
        L"1 < 2", L"2 > 1", L"(1)", L"(2 + 3) * (4 - 1)"
    };
    for (size_t i = 0; i < sizeof(expressions) / sizeof(*expressions); i++) {
        arena_t *arena = create_arena(8);
        parser_memory_t memory = { arena, arena, arena, arena };
        string_value_t source = format_string(
            L"var x = 0;\nx = %s;\nvar y = x;\nconst z = 1 + x;\n", expressions[i]);
        node_t *root = parse_program(&memory, source);
        ASSERT(root);
        options_t *options = create_options();
        analysis_collector_t *collector = create_analysis_collector(arena);
        ASSERT(!analyze(root, &memory, options, collector));
        analysis_event_query_t query = { .kind = ANALYSIS_VALUE_WRITE, .row = 2 };
        const analysis_event_t *write = find_analysis_event(collector, NULL, &query);
        ASSERT(write && write->value->type == LATTICE_TOP);
        query = (analysis_event_query_t){ .kind = ANALYSIS_DECLARATION_SUMMARY,
            .declarator = write->declarator };
        const analysis_event_t *summary = find_last_analysis_event(collector, &query);
        ASSERT(summary && summary->value->type == LATTICE_TOP);
        ASSERT(summary->declarator->abstract_value == summary->value);
        query = (analysis_event_query_t){ .kind = ANALYSIS_DECLARATION_SUMMARY, .row = 3 };
        summary = find_last_analysis_event(collector, &query);
        ASSERT(summary && summary->value->type == LATTICE_TOP);
        query.row = 4;
        summary = find_last_analysis_event(collector, &query);
        ASSERT(summary && summary->value->type == LATTICE_NUMERIC);
        query = (analysis_event_query_t){ .kind = ANALYSIS_VALUE_WRITE, .row = 1 };
        const analysis_event_t *initial = find_analysis_event(collector, NULL, &query);
        ASSERT(initial && initial->value->type == LATTICE_INTEGER_CONSTANT);
        ASSERT(((const integer_constant_element_t *)initial->value)->value == 0);
        destroy_options(options);
        destroy_arena(arena);
        FREE_STRING(source);
    }
    return true;
}

static bool check_valueless_nodes(node_t *node, abstract_state_t *state,
        arena_t *arena, bool *seen) {
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
    parser_memory_t memory = { arena, arena, arena, arena };
    node_t *root = parse_program(&memory, STATIC_STRING(
        L"var x = 1; const f = func(a) { if (a) { return a; } else { return 0; } }; x = 2;"));
    ASSERT(root);
    abstract_state_t *state = create_abstract_state(arena);
    state->collector = create_analysis_collector(arena);
    bool seen[NODE_DO_WHILE + 1] = {0};
    ASSERT(check_valueless_nodes(root, state, arena, seen));
    node_type_t types[] = { NODE_ROOT, NODE_ARGUMENT, NODE_ARGUMENT_LIST, NODE_FUNCTION_BODY,
        NODE_VARIABLE_DECLARATOR, NODE_CONSTANT_DECLARATOR, NODE_VARIABLE_DECLARATION,
        NODE_CONSTANT_DECLARATION, NODE_STATEMENT_EXPRESSION, NODE_RETURN, NODE_IF_ELSE };
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++) {
        ASSERT(seen[types[i]]);
    }
    ASSERT(state->collector->count == 0 && state->control_flow == FLOW_NORMAL);
    expression_t *one = (expression_t *)create_integer_node(arena, 1);
    expression_t *two = (expression_t *)create_integer_node(arena, 2);
    const lattice_element_t *sum = calculate_expression(
        create_addition_node(arena, one, two), state, arena);
    ASSERT(sum->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(((const integer_constant_element_t *)sum)->value == 3);
    expression_t *invalid = create_addition_node(arena,
        (expression_t *)create_true_node(arena), one);
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
