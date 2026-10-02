/**
 * @file test_optimization.c
 * @copyright 2026 Ivan Kniazkov
 * @brief CLI modes, mandatory binding, and conditional bytecode pruning.
 */
#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "graph/declarations.h"
#include "graph/variable.h"
#include "test_macro.h"

#include <stdio.h>
#include <wchar.h>

bool test_optimization_options() {
    options_t *options = create_options();
    ASSERT(options->optimization_level == OPTIMIZATION_ALL);
    destroy_options(options);
    char *args[] = {"goat", "--optimize", "none", "--optimize", "all", "test.goat"};
    options = parse_options(6, args);
    ASSERT(options && options->optimization_level == OPTIMIZATION_ALL);
    destroy_options(options);
    char *disabled[] = {"goat", "test.goat", "--optimize", "none"};
    options = parse_options(4, disabled);
    ASSERT(options && options->optimization_level == OPTIMIZATION_NONE);
    destroy_options(options);
    return true;
}

static bool has_dead_node(const node_t *node) {
    if (node->unreachable)
        return true;
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        if (has_dead_node(get_node_child(node, i)))
            return true;
    }
    return false;
}

static size_t opcode_count(const code_builder_t *code, unsigned opcode) {
    size_t count = 0;
    for (size_t i = 0; i < code->size; i++)
        count += code->instructions[i].opcode == opcode;
    return count;
}

bool test_optimization_modes() {
    for (int disabled = 0; disabled < 2; disabled++) {
        arena_t *arena = create_arena(16);
        parser_memory_t memory = {arena, arena, arena, arena};
        node_t *root = parse_analysis_test_program(
            &memory,
            STATIC_STRING(L"var x = 0; if (true) { x = 1; } else { x = 2; } print(x);"));
        ASSERT(root);
        options_t *options = create_options();
        options->optimization_level = disabled ? OPTIMIZATION_NONE : OPTIMIZATION_ALL;
        analysis_collector_t *collector = create_analysis_collector(arena);
        ASSERT(!analyze(root, &memory, options, collector));
        ASSERT((collector->count == 0) == disabled);
        ASSERT(has_dead_node(root) == !disabled);
        declarator_t *x = (declarator_t *)get_node_child(get_node_child(root, 0), 0);
        ASSERT((x->abstract_value == NULL) == disabled);
        code_builder_t *code = create_code_builder();
        data_builder_t *data = create_data_builder();
        generate_bytecode_from_node(root, code, data);
        ASSERT(code->size == (disabled ? 22 : 13));
        ASSERT(opcode_count(code, TRUE) == disabled);
        ASSERT(opcode_count(code, JIF) == disabled);
        ASSERT(opcode_count(code, JUMP) == disabled);
        destroy_data_builder(data);
        destroy_code_builder(code);
        destroy_options(options);
        destroy_arena(arena);
    }
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(&memory, STATIC_STRING(L"x;"));
    ASSERT(root);
    options_t *options = create_options();
    options->optimization_level = OPTIMIZATION_NONE;
    ASSERT(!analyze(root, &memory, options, NULL));
    ASSERT(get_node_child_count(root) == 2);
    node_t *declaration = get_node_child(root, 0);
    variable_t *use = (variable_t *)get_node_child(get_node_child(root, 1), 0);
    ASSERT(use->declarator == (declarator_t *)get_node_child(declaration, 0));
    ASSERT(use->base.base.base.parent && use->base.base.base.scope && use->base.base.base.id);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_optimized_if_bytecode() {
    struct {
        const wchar_t *source;
        size_t jumps, stores, pops, functions;
    } cases[] = {{L"if (true) { } else { }", 0, 0, 1, 0},
                 {L"if ((true)) { }", 0, 0, 1, 0},
                 {L"if (false) { }", 0, 0, 0, 0},
                 {L"if (null) { } else { }", 0, 0, 1, 0},
                 {L"var x = 0; if ((x = 1)) { } else { }", 0, 1, 2, 0},
                 {L"var x = 1; if ((x = 0)) { }", 0, 1, 1, 0},
                 {L"var x = 1; if (x) { }", 0, 0, 2, 0},
                 {L"if (func() {}) { }", 0, 0, 2, 1},
                 {L"if (1 < 2) { } else { }", 0, 0, 2, 0},
                 {L"if (pi) { } else { }", 2, 0, 2, 0},
                 {L"if ({ return; }) { } else { }", 0, 0, 0, 0}};

    for (size_t c = 0; c < sizeof(cases) / sizeof(*cases); c++) {
        arena_t *arena = create_arena(16);
        parser_memory_t memory = {arena, arena, arena, arena};
        node_t *root = parse_analysis_test_program(
            &memory,
            (string_value_t){cases[c].source, wcslen(cases[c].source), false});
        ASSERT(root);
        options_t *options = create_options();
        ASSERT(!analyze(root, &memory, options, NULL));
        code_builder_t *code = create_code_builder();
        data_builder_t *data = create_data_builder();
        generate_bytecode_from_node(root, code, data);
        ASSERT(opcode_count(code, JIF) + opcode_count(code, JUMP) == cases[c].jumps);
        ASSERT(opcode_count(code, STORE) == cases[c].stores);
        ASSERT(opcode_count(code, POP) == cases[c].pops);
        ASSERT(opcode_count(code, FUNC) == cases[c].functions);
        ASSERT(!opcode_count(code, TRUE) && !opcode_count(code, FALSE));
        destroy_data_builder(data);
        destroy_code_builder(code);
        destroy_options(options);
        destroy_arena(arena);
    }
    return true;
}
