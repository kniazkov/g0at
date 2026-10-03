/** @file test_replacement.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Rewrites preserve values, history, lexical scopes, and effectful evaluation.
 */
#include "test_replacement.h"

#include "analysis/analysis.h"
#include "analysis/simplification.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "graph/binary_operation.h"
#include "graph/replacement.h"
#include "graph/visualization.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <math.h>
#include <stdio.h>
#include <wchar.h>

static size_t opcode_count(const code_builder_t *code, opcode_t opcode) {
    size_t count = 0;
    for (size_t i = 0; i < code->size; i++)
        count += code->instructions[i].opcode == opcode;
    return count;
}

static node_t *initializer(node_t *root, size_t index) {
    return get_node_child(get_node_child(get_node_child(root, index), 0), 0);
}

/** @brief Counts executable nodes, excluding historical edges. */
static size_t count_type(const node_t *node, node_type_t type) {
    size_t count = node->vtbl->type == type;
    size_t start = is_replacement(node) ? 1 : 0;
    for (size_t i = start; i < get_node_child_count(node); i++)
        count += count_type(get_node_child(node, i), type);
    return count;
}

bool test_replacement_nodes() {
    arena_t *arena = create_arena(16);
    expression_t *old = create_addition_node(arena,
                                             (expression_t *)create_integer_node(arena, 2),
                                             (expression_t *)create_integer_node(arena, 3));
    expression_t *value = (expression_t *)create_integer_node(arena, 5);
    node_t *node = (node_t *)create_expression_replacement(arena, old, value);
    ASSERT(is_expression(node->vtbl->type) && !node->vtbl->is_assignable_expression);
    ASSERT(get_node_child_count(node) == 2);
    ASSERT(get_node_child(node, 0) == (node_t *)old && get_node_child(node, 1) == (node_t *)value);
    ASSERT(!get_node_child(node, 2) && !get_node_child_tag(node, 2));
    ASSERT(!wcscmp(get_node_child_tag(node, 0), L"original"));
    ASSERT(!wcscmp(get_node_child_tag(node, 1), L"replacement"));
    ASSERT(!replace_child_node(node, (node_t *)old, (node_t *)value));
    abstract_state_t *state = create_abstract_state(arena);
    const lattice_element_t *result = calculate_node(node, state, arena);
    ASSERT(result->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(((const integer_constant_element_t *)result)->value == 5);
    string_value_t source = generate_goat_code_from_node(node);
    ASSERT(!wcscmp(source.data, L"5"));
    FREE_STRING(source);
    code_builder_t *code = create_code_builder();
    data_builder_t *data = create_data_builder();
    generate_bytecode_from_node(node, code, data);
    ASSERT(code->size == 1 && code->instructions[0].opcode == ILOAD32
           && code->instructions[0].arg1 == 5);
    node_t *original = create_throw_node(arena, (expression_t *)create_null_node(arena));
    statement_t *empty = create_statement_expression_node(arena, NULL);
    node = (node_t *)create_statement_replacement(arena, (statement_t *)original, empty);
    ASSERT(is_statement(node->vtbl->type) && get_node_child_count(node) == 2);
    ASSERT(execute_node(node, state, arena) == state && state->control_flow == FLOW_NORMAL);
    generate_bytecode_from_node(node, code, data);
    ASSERT(code->size == 1 && !opcode_count(code, THROW));
    ASSERT(generate_deferred_bytecode_from_node(node, code, data));
    destroy_data_builder(data);
    destroy_code_builder(code);
    destroy_abstract_state(state);
    destroy_arena(arena);
    return true;
}

bool test_replacement_folding() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"var a = 2 + 3 * 4; var b = 1 + 0.5; var c = \"a\" + \"b\";\n"
                      L"var d = 1 < 2; var e = -0.0; var f = null; var g = f;\n"
                      L"var h = (-1.0) ** 0.5; var i = 0.0 ** -1;\n"
                      L"var x=1; var y=x+1; x=8; var z=x+1;"));
    ASSERT(root);
    options_t *options = create_options();
    ASSERT(!analyze(root, &memory, options, NULL));
    const node_type_t types[] = {NODE_INTEGER,
                                 NODE_REAL,
                                 NODE_STATIC_STRING,
                                 NODE_TRUE,
                                 NODE_REAL,
                                 NODE_NULL,
                                 NODE_NULL,
                                 NODE_REAL,
                                 NODE_REAL};
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++) {
        node_t *expr = initializer(root, i);
        if (i != 5) {
            ASSERT(is_replacement(expr));
            ASSERT(get_node_child(expr, 0)->parent == expr);
            ASSERT(get_node_child(expr, 1)->parent == expr);
            ASSERT(expr->position == get_node_child(expr, 0)->position);
        }
        ASSERT(replacement_result(expr)->vtbl->type == types[i]);
    }
    abstract_state_t *state = create_abstract_state(arena);
    const lattice_element_t *zero = calculate_node(initializer(root, 4), state, arena);
    ASSERT(signbit(((const real_constant_element_t *)zero)->value));
    const lattice_element_t *nan = calculate_node(initializer(root, 7), state, arena);
    ASSERT(isnan(((const real_constant_element_t *)nan)->value));
    const lattice_element_t *inf = calculate_node(initializer(root, 8), state, arena);
    ASSERT(isinf(((const real_constant_element_t *)inf)->value));
    const lattice_element_t *y = calculate_node(initializer(root, 10), state, arena);
    const lattice_element_t *z = calculate_node(initializer(root, 12), state, arena);
    ASSERT(((const integer_constant_element_t *)y)->value == 2);
    ASSERT(((const integer_constant_element_t *)z)->value == 9);
    destroy_abstract_state(state);
    code_builder_t *code = create_code_builder();
    data_builder_t *data = create_data_builder();
    generate_bytecode_from_node(root, code, data);
    ASSERT(!opcode_count(code, ADD) && !opcode_count(code, MUL));
    ASSERT(opcode_count(code, STORE) == 1);
    destroy_data_builder(data);
    destroy_code_builder(code);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_replacement_branches() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"var x=0; if (1<2) {var x=1; print(x);} else {print(9);}\n"
                      L"if (false) print(8);\n"
                      L"if (false) print(7) else if (true) print(2) else print(6);"));
    ASSERT(root);
    node_t *original = get_node_child(root, 1);
    node_t *selected = get_node_child(original, 1);
    options_t *options = create_options();
    ASSERT(!analyze(root, &memory, options, NULL));
    node_t *wrapper = get_node_child(root, 1);
    ASSERT(wrapper->vtbl->type == NODE_STATEMENT_REPLACEMENT);
    ASSERT(get_node_child(wrapper, 0) == original && get_node_child(wrapper, 1) == selected);
    ASSERT(selected->parent == wrapper && original->parent == wrapper);
    ASSERT(get_node_child(selected, 0)->scope != root->scope);
    ASSERT(!count_type(root, NODE_IF_ELSE));
    string_value_t dot = generate_graph_dot(root);
    ASSERT(wcsstr(dot.data, L"statement replacement"));
    ASSERT(wcsstr(dot.data, L"expression replacement"));
    ASSERT(wcsstr(dot.data, L"style=\"rounded,filled\" fillcolor=\"#f5efff\""));
    ASSERT(wcsstr(dot.data, L"if-else") && wcsstr(dot.data, L"color=lightgray"));
    FREE_STRING(dot);
    code_builder_t *code = create_code_builder();
    data_builder_t *data = create_data_builder();
    generate_bytecode_from_node(root, code, data);
    ASSERT(!opcode_count(code, JIF) && !opcode_count(code, JUMP));
    ASSERT(opcode_count(code, CALL) == 2 && opcode_count(code, ENTER) == 1);
    destroy_data_builder(data);
    destroy_code_builder(code);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_replacement_boundaries() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"const f=func(n){return n+1;};f(1);f(2);\n"
                      L"const closed=func(n){return n+(2*3);};closed(4);\n"
                      L"var x=0;if ((x=1)) print(x);++x;x++;\n"
                      L"if (sqrt(1)) print(x);\n"
                      L"try { 1 + true; } catch(e) { print(e); }"));
    ASSERT(root);
    options_t *options = create_options();
    ASSERT(!analyze(root, &memory, options, NULL));
    ASSERT(count_type(root, NODE_IF_ELSE) == 2);
    ASSERT(count_type(root, NODE_FUNCTION_CALL) == 7);
    ASSERT(count_type(root, NODE_ADDITION) == 3);
    ASSERT(!count_type(root, NODE_MULTIPLICATION));
    ASSERT(count_type(root, NODE_SIMPLE_ASSIGNMENT) == 1);
    ASSERT(count_type(root, NODE_PREFIX_INCREMENT) == 1);
    ASSERT(count_type(root, NODE_POSTFIX_INCREMENT) == 1);
    /* A replacement is never accepted as an assignment target. */
    node_t *update = get_node_child(get_node_child(root, 7), 0);
    ASSERT(update->vtbl->type == NODE_PREFIX_INCREMENT);
    ASSERT(get_node_child(update, 0)->vtbl->type == NODE_VARIABLE);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_replacement_reset() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root =
        parse_analysis_test_program(&memory,
                                    STATIC_STRING(L"var x=2+3; if (x==5) print(x) else print(9);"));
    options_t *options = create_options();
    ASSERT(root && !analyze(root, &memory, options, NULL));
    string_value_t first = generate_graph_dot(root);
    simplify_graph(root, arena);
    string_value_t second = generate_graph_dot(root);
    ASSERT(!wcscmp(first.data, second.data));
    FREE_STRING(second);
    ASSERT(!analyze(root, &memory, options, NULL));
    ASSERT(count_type(root, NODE_STATEMENT_REPLACEMENT) == 1);
    options->optimization_level = OPTIMIZATION_NONE;
    ASSERT(!analyze(root, &memory, options, NULL));
    ASSERT(!count_type(root, NODE_STATEMENT_REPLACEMENT));
    ASSERT(!count_type(root, NODE_EXPRESSION_REPLACEMENT));
    ASSERT(count_type(root, NODE_ADDITION) == 1 && count_type(root, NODE_IF_ELSE) == 1);
    second = generate_graph_dot(root);
    ASSERT(!wcsstr(second.data, L"#f5efff"));
    FREE_STRING(second);
    FREE_STRING(first);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_replacement_abs() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"var a=abs(10); var b=abs(-0.0); var c=abs((-1)**0.5);\n"
                      L"var d=abs(-(0**-1)); const alias=abs; var e=alias(-5);\n"
                      L"var f=abs(abs(-3)); var g=abs(2,3);"));
    options_t *options = create_options();
    ASSERT(root && !analyze(root, &memory, options, NULL));
    ASSERT(!count_type(root, NODE_FUNCTION_CALL));
    const size_t indices[] = {0, 1, 2, 3, 5, 6, 7};
    const double expected[] = {10.0, 0.0, NAN, INFINITY, 5.0, 3.0, 2.0};
    abstract_state_t *state = create_abstract_state(arena);
    for (size_t i = 0; i < sizeof(indices) / sizeof(*indices); i++) {
        node_t *expr = initializer(root, indices[i]);
        ASSERT(is_replacement(expr));
        ASSERT(get_node_child(expr, 0)->vtbl->type == NODE_FUNCTION_CALL);
        const lattice_element_t *value = calculate_node(expr, state, arena);
        ASSERT(value->type == LATTICE_REAL_CONSTANT);
        double number = ((const real_constant_element_t *)value)->value;
        ASSERT(isnan(expected[i]) ? isnan(number) : number == expected[i]);
        if (i == 1) {
            ASSERT(!signbit(number));
        }
    }
    code_builder_t *code = create_code_builder();
    data_builder_t *data = create_data_builder();
    generate_bytecode_from_node(root, code, data);
    ASSERT(!opcode_count(code, CALL));
    string_value_t dot = generate_graph_dot(root);
    ASSERT(wcsstr(dot.data, L"color=purple style=\"rounded,filled\" fillcolor=\"#f5efff\""));
    FREE_STRING(dot);
    destroy_data_builder(data);
    destroy_code_builder(code);
    destroy_abstract_state(state);
    destroy_options(options);
    destroy_arena(arena);

    arena = create_arena(16);
    memory = (parser_memory_t){arena, arena, arena, arena};
    root = parse_analysis_test_program(&memory,
                                       STATIC_STRING(L"var x=0;abs(x=1);abs(1,x=2);\n"
                                                     L"var abs=func(n){return n+1;};abs(3);"));
    options = create_options();
    ASSERT(root && !analyze(root, &memory, options, NULL));
    ASSERT(count_type(root, NODE_FUNCTION_CALL) == 3);
    ASSERT(count_type(root, NODE_SIMPLE_ASSIGNMENT) == 2);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
