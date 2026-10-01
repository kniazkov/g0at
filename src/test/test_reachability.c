/**
 * @file test_reachability.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Reachability flags, collector events, bytecode, and graph rendering.
 */
#include <wchar.h>
#include <stdio.h>
#include "test_macro.h"
#include "analysis_test_support.h"
#include "analysis/analysis.h"
#include "analysis/reachability.h"
#include "cli/options.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "graph/expression.h"
#include "graph/statement.h"
#include "graph/visualization.h"
#include "lib/allocate.h"

static bool subtree_has_flag(const node_t *node, bool flag) {
    if (node->unreachable != flag) return false;
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        if (!subtree_has_flag(get_node_child(node, i), flag)) return false;
    }
    return true;
}

bool test_reachability_flags() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(&memory, STATIC_STRING(
        L"var x = 0;\nif (false) { x = 17; var f = func() { return 18; }; }\n"
        L"else { x = 23; }\nvar f = func() { if (false) { return 31; } return 32; };\n"
        L"return;\nx = 99;"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    node_t *conditional = get_node_child(root, 1);
    node_t *dead = get_node_child(conditional, 1);
    ASSERT(subtree_has_flag(dead, true));
    ASSERT(subtree_has_flag(get_node_child(conditional, 2), false));
    ASSERT(subtree_has_flag(get_node_child(root, 2), false));
    ASSERT(subtree_has_flag(get_node_child(root, 4), true));
    analysis_event_query_t query = {.kind = ANALYSIS_UNREACHABLE, .node = dead};
    const analysis_event_t *event = find_analysis_event(collector, NULL, &query);
    ASSERT(event && !event->declarator && !event->value && event->row == 2);
    ASSERT(!find_analysis_event(collector, event, &query));
    string_value_t text = analysis_collector_to_text(collector);
    ASSERT(wcsstr(text.data, L"test.goat, 2.12: unreachable statement expression"));
    FREE_STRING(text);
    /* Re-running proofs resets old flags, including untouched function bodies. */
    get_node_child(root, 2)->unreachable = true;
    mark_unreachable_code(root, arena, NULL);
    ASSERT(subtree_has_flag(get_node_child(root, 2), false));
    ASSERT(subtree_has_flag(dead, true));
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_reachability_bytecode() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(&memory, STATIC_STRING(
        L"var x = 0; if ((x = 1)) { x = 23; } else { x = 99; } return; x = 98;"));
    ASSERT(root);
    options_t *options = create_options();
    ASSERT(!analyze(root, &memory, options, NULL));
    code_builder_t *code = create_code_builder();
    data_builder_t *data = create_data_builder();
    generate_bytecode_from_node(root, code, data);
    size_t writes = 0, live = 0, branches = 0;
    for (size_t i = 0; i < code->size; i++) {
        instruction_t instr = code->instructions[i];
        if (instr.opcode == STORE) writes++;
        if (instr.opcode == ILOAD32) {
            ASSERT(instr.arg1 != 99 && instr.arg1 != 98);
            if (instr.arg1 == 23) live++;
        }
        if (instr.opcode == JIF || instr.opcode == JUMP) {
            ASSERT(instr.arg1 < code->size);
            branches++;
        }
    }
    ASSERT(writes == 2 && live == 1 && branches == 2);
    destroy_data_builder(data);
    destroy_code_builder(code);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_reachability_bytecode_boundaries() {
    const wchar_t *sources[] = {
        L"if (false) { var x = 99; }",
        L"if (true) { } else { var x = 99; }",
        L"if (1 < 2) { return; } else { return; } var x = 99;",
        L"var a = { return; }, b = 99;",
        L"const a = { return; }, b = 99;",
        L"var f = func(a, b) {}; f(99, { return; });",
        L"return; var f = func() { return 99; };"
    };
    for (size_t c = 0; c < sizeof(sources) / sizeof(*sources); c++) {
        arena_t *arena = create_arena(16);
        parser_memory_t memory = {arena, arena, arena, arena};
        node_t *root = parse_analysis_test_program(&memory,
            (string_value_t){sources[c], wcslen(sources[c]), false});
        ASSERT(root);
        options_t *options = create_options();
        ASSERT(!analyze(root, &memory, options, NULL));
        code_builder_t *code = create_code_builder();
        data_builder_t *data = create_data_builder();
        generate_bytecode_from_node(root, code, data);
        for (size_t i = 0; i < code->size; i++) {
            instruction_t instr = code->instructions[i];
            ASSERT(instr.opcode != ILOAD32 || instr.arg1 != 99);
            if (c == 6) ASSERT(instr.opcode != FUNC);
            if (instr.opcode == JIF || instr.opcode == JUMP) ASSERT(instr.arg1 < code->size);
        }
        destroy_data_builder(data);
        destroy_code_builder(code);
        destroy_options(options);
        destroy_arena(arena);
    }
    return true;
}

bool test_reachability_graph() {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(&memory, STATIC_STRING(
        L"var x = 1; if (false) { var y = x; print(\"dead\"); } else { print(x); }"));
    ASSERT(root);
    options_t *options = create_options();
    ASSERT(!analyze(root, &memory, options, NULL));
    string_value_t dot = generate_graph_dot(root);
    ASSERT(wcsstr(dot.data, L"fillcolor=gray96 tooltip=\"unreachable\""));
    ASSERT(wcsstr(dot.data, L"font color='gray70'>\"dead\"</font>"));
    ASSERT(wcsstr(dot.data, L"color=lightgray fontcolor=gray70"));
    ASSERT(wcsstr(dot.data, L"style=dashed, color=lightgray, fontcolor=gray70"));
    ASSERT(wcsstr(dot.data, L"style=dashed, color=navy, fontcolor=black"));
    ASSERT(wcsstr(dot.data, L"color=black"));
    FREE_STRING(dot);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
