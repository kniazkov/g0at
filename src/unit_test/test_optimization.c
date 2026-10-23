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
#include "codegen/linker.h"
#include "codegen/source_builder.h"
#include "graph/declarations.h"
#include "graph/replacement.h"
#include "graph/variable.h"
#include "graph/visualization.h"
#include "lib/allocate.h"
#include "lib/file_system.h"
#include "lib/string_ext.h"
#include "model/process.h"
#include "test_macro.h"
#include "vm/bytecode.h"
#include "vm/vm.h"

#ifdef _WIN32
#    include <io.h>
#    define DUP_FD _dup
#    define DUP2_FD _dup2
#    define CLOSE_FD _close
#    define FILENO_FD _fileno
#else
#    include <unistd.h>
#    define DUP_FD dup
#    define DUP2_FD dup2
#    define CLOSE_FD close
#    define FILENO_FD fileno
#endif
#include "goat/unicode.h"

#include <stdio.h>
#include <string.h>

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
    if (node_has_flag(node, NODE_FLAG_UNREACHABLE))
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
            STATIC_STRING(U"var x = 0; if (true) { x = 1; } else { x = 2; } print(x);"));
        ASSERT(root);
        options_t *options = create_options();
        options->optimization_level = disabled ? OPTIMIZATION_NONE : OPTIMIZATION_ALL;
        analysis_collector_t *collector = create_analysis_collector(arena);
        ASSERT(!analyze(root, &memory, options, collector));
        ASSERT((collector->count == 0) == disabled);
        ASSERT(has_dead_node(root) == !disabled);
        declarator_t *x =
            (declarator_t *)replacement_original(get_node_child(get_node_child(root, 0), 0));
        ASSERT((x->abstract_value == NULL) == disabled);
        code_builder_t *code = create_code_builder();
        data_builder_t *data = create_data_builder();
        generate_bytecode_from_node(root, code, data);
        ASSERT(code->size == (disabled ? 22 : 8));
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
    node_t *root = parse_analysis_test_program(&memory, STATIC_STRING(U"x;"));
    ASSERT(root);
    options_t *options = create_options();
    options->optimization_level = OPTIMIZATION_NONE;
    ASSERT(!analyze(root, &memory, options, NULL));
    ASSERT(get_node_child_count(root) == 1);
    variable_t *use = (variable_t *)get_node_child(get_node_child(root, 0), 0);
    ASSERT(use->declarator == NULL);
    ASSERT(use->base.base.base.parent && use->base.base.base.scope && use->base.base.base.id);
    destroy_options(options);
    destroy_arena(arena);
    arena = create_arena(16);
    memory = (parser_memory_t){arena, arena, arena, arena};
    root = parse_analysis_test_program(&memory, STATIC_STRING(U"x = 1;"));
    ASSERT(root);
    options = create_options();
    options->optimization_level = OPTIMIZATION_NONE;
    ASSERT(!analyze(root, &memory, options, NULL));
    ASSERT(get_node_child_count(root) == 1);
    node_t *declaration = get_node_child(root, 0);
    ASSERT(declaration->vtbl->type == NODE_CONSTANT_DECLARATION);
    ASSERT(get_node_child_count(declaration) == 1);
    ASSERT(get_node_child(declaration, 0)->vtbl->type == NODE_CONSTANT_DECLARATOR);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_optimized_if_bytecode() {
    struct {
        const gchar_t *source;
        size_t jumps, stores, pops, functions;
    } cases[] = {{U"if (true) { } else { }", 0, 0, 1, 0},
                 {U"if ((true)) { }", 0, 0, 1, 0},
                 {U"if (false) { }", 0, 0, 0, 0},
                 {U"if (null) { } else { }", 0, 0, 1, 0},
                 {U"var x = 0; if ((x = 1)) { } else { }", 0, 0, 2, 0},
                 {U"var x = 1; if ((x = 0)) { }", 0, 0, 1, 0},
                 {U"var x = 1; if (x) { }", 0, 0, 1, 0},
                 {U"if (func() {}) { }", 0, 0, 2, 1},
                 {U"if (1 < 2) { } else { }", 0, 0, 1, 0},
                 {U"if (pi) { } else { }", 2, 0, 2, 0},
                 {U"if ({ return; }) { } else { }", 0, 0, 0, 0}};

    for (size_t c = 0; c < sizeof(cases) / sizeof(*cases); c++) {
        arena_t *arena = create_arena(16);
        parser_memory_t memory = {arena, arena, arena, arena};
        node_t *root = parse_analysis_test_program(
            &memory,
            (string_value_t){cases[c].source, STRLEN(cases[c].source), false});
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

bool test_unused_bindings() {
    struct {
        const gchar_t *source;
        size_t vars, stores, calls;
        const gchar_t *printed;
    } cases[] = {
        {U"a=2;b=3;x=a+b;println(x);", 0, 0, 1, U"println(5);"},
        {U"var a=1;var b=a;const c=b;", 0, 0, 0, U""},
        {U"var a=input();", 0, 0, 1, U"input();"},
        {U"var a=input(); const unused=func(){return a;};", 0, 0, 1, U"input();"},
        {U"var a=input(),b=input();println(b);", 1, 0, 3, U"input(); var b = input(); println(b);"},
        {U"a=input();println(a);", 0, 0, 2, U"const a = input(); println(a);"},
        {U"var a=1;const f=func(){return a;};println(f());",
         1,
         0,
         2,
         U"var a = 1; const f = func() {return a;}; println(f());"},
        {U"var a=1; a++;", 1, 1, 0, U"var a = 1; a++;"},
        {U"const a=1;try{a=2;}catch(e){println(e);}",
         1,
         1,
         1,
         U"const a = 1; try {a = 2;} catch (e) {println(e);}"},
        {U"for (var i = 0, j = 5; i < 2; i++) println(i);",
         1,
         1,
         1,
         U"for (var i = 0; i < 2; i++) {println(i);}"},
        {U"for (var i = 0, j = input(); i < 2; i++) println(i);",
         1,
         1,
         2,
         U"for (var i = 0, j = input(); i < 2; i++) {println(i);}"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        arena_t *arena = create_arena(16);
        parser_memory_t memory = {arena, arena, arena, arena};
        node_t *root = parse_analysis_test_program(
            &memory,
            (string_value_t){cases[i].source, STRLEN(cases[i].source), false});
        options_t *options = create_options();
        ASSERT(root && !analyze(root, &memory, options, NULL));
        code_builder_t *code = create_code_builder();
        data_builder_t *data = create_data_builder();
        generate_bytecode_from_node(root, code, data);
        ASSERT(opcode_count(code, VAR) == cases[i].vars);
        ASSERT(opcode_count(code, STORE) == cases[i].stores);
        ASSERT(opcode_count(code, CALL) == cases[i].calls);
        string_value_t printed = generate_goat_code_from_node(root);
        ASSERT(!STRCMP(printed.data, cases[i].printed));
        FREE_STRING(printed);
        if (i == 0) {
            ASSERT(code->size == 5 && code->instructions[0].opcode == ILOAD32
                   && code->instructions[0].arg1 == 5);
            const node_t *deleted = get_node_child(get_node_child(root, 0), 0);
            ASSERT(is_deletion(deleted) && get_node_child_count(deleted) == 1);
            string_value_t dot = generate_graph_dot(root);
            ASSERT(STRSTR(dot.data, U"darkred") && STRSTR(dot.data, U"#ffe5e5"));
            ASSERT(STRSTR(dot.data, U"midnightblue") && STRSTR(dot.data, U"#eaf1fa"));
            FREE_STRING(dot);
            options->optimization_level = OPTIMIZATION_NONE;
            ASSERT(!analyze(root, &memory, options, NULL));
            ASSERT(get_node_child(get_node_child(root, 0), 0)->vtbl->type
                   == NODE_CONSTANT_DECLARATOR);
            options->optimization_level = OPTIMIZATION_ALL;
            ASSERT(!analyze(root, &memory, options, NULL));
        }
        destroy_data_builder(data);
        destroy_code_builder(code);
        destroy_options(options);
        destroy_arena(arena);
    }
    return true;
}

/** @brief Reads and UTF-8 decodes the whole captured stream; caller frees the result. */
static string_value_t read_captured_file(FILE *file) {
    FFLUSH(file);
    if (FSEEK(file, 0, SEEK_END) != 0)
        return NULL_STRING_VALUE;
    long size = FTELL(file);
    if (size < 0 || FSEEK(file, 0, SEEK_SET) != 0)
        return NULL_STRING_VALUE;
    char *buffer = (char *)ALLOC((size_t)size + 1);
    size_t read = FREAD(buffer, 1, (size_t)size, file);
    buffer[read] = 0;
    string_value_t result = decode_utf8(buffer);
    FREE(buffer);
    return result;
}

/**
 * @brief Compiles and runs a program with redirected stdin/stdout.
 *
 * The stdout produced by the program is returned as a decoded string; NULL_STRING_VALUE
 * indicates a compile error, an uncaught exception, or an I/O failure.
 */
static string_value_t run_captured_program(const gchar_t *source, const gchar_t *input) {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root =
        parse_analysis_test_program(&memory, (string_value_t){source, STRLEN(source), false});
    string_value_t output = NULL_STRING_VALUE;
    if (root) {
        options_t *options = create_options();
        if (!analyze(root, &memory, options, NULL)) {
            code_builder_t *code = create_code_builder();
            data_builder_t *data = create_data_builder();
            generate_bytecode_from_node(root, code, data);
            bytecode_t *bytecode = link_code_and_data(code, data);
            destroy_code_builder(code);
            destroy_data_builder(data);
            const char *input_path = "unit-test-input.tmp";
            const char *output_path = "unit-test-output.tmp";
            FILE *input_file = fopen(input_path, "w+b");
            if (input) {
                char *encoded = encode_utf8(input);
                FWRITE(encoded, 1, strlen(encoded), input_file);
                FREE(encoded);
            }
            REWIND(input_file);
            FILE *output_file = fopen(output_path, "w+b");
            FFLUSH(stdout);
            int saved_stdin = DUP_FD(FILENO_FD(stdin));
            int saved_stdout = DUP_FD(FILENO_FD(stdout));
            DUP2_FD(FILENO_FD(input_file), FILENO_FD(stdin));
            DUP2_FD(FILENO_FD(output_file), FILENO_FD(stdout));
            FSEEK(stdin, 0, SEEK_SET);
            CLEARERR(stdin);
            CLEARERR(stdout);
            process_t *proc = create_process();
            int status = run(proc, bytecode);
            FFLUSH(stdout);
            DUP2_FD(saved_stdout, FILENO_FD(stdout));
            DUP2_FD(saved_stdin, FILENO_FD(stdin));
            CLOSE_FD(saved_stdin);
            CLOSE_FD(saved_stdout);
            if (!status) {
                REWIND(output_file);
                output = read_captured_file(output_file);
            }
            destroy_process(proc);
            free_bytecode(bytecode);
            FCLOSE(input_file);
            FCLOSE(output_file);
            remove(input_path);
            remove(output_path);
        }
        destroy_options(options);
    }
    destroy_arena(arena);
    return output;
}

bool test_printed_source_recompiles() {
    struct {
        const gchar_t *source;
        const gchar_t *input;
        const gchar_t *single;
        const gchar_t *indented;
        const gchar_t *output;
    } cases[] = {
        {U"for (var a=println(1), b=println(2); false;) {}",
         NULL,
         U"for (var a = println(1), b = println(2); false; ) { }",
         U"for (var a = println(1), b = println(2); false; ) { }\n",
         U"1\n2\n"},
        {U"for (const a=println(1), b=println(2); false;) {}",
         NULL,
         U"for (const a = println(1), b = println(2); false; ) { }",
         U"for (const a = println(1), b = println(2); false; ) { }\n",
         U"1\n2\n"},
        {U"if (input()) var a=println(1), b=println(2);\nprintln(\"after\");",
         U"",
         U"if (input()) { println(1); println(2); } println(\"after\");",
         U"if (input()) {\n    println(1);\n    println(2);\n}\nprintln(\"after\");\n",
         U"after\n"},
        {U"if (input()) const a=println(1), b=println(2);\nprintln(\"after\");",
         U"",
         U"if (input()) { println(1); println(2); } println(\"after\");",
         U"if (input()) {\n    println(1);\n    println(2);\n}\nprintln(\"after\");\n",
         U"after\n"},
        {U"if (input()) var a=println(1), b=println(2);\nprintln(\"after\");",
         U"x",
         U"if (input()) { println(1); println(2); } println(\"after\");",
         U"if (input()) {\n    println(1);\n    println(2);\n}\nprintln(\"after\");\n",
         U"1\n2\nafter\n"},
        {U"if (input()) var x=1;\nprintln(\"after\");",
         U"",
         U"if (input()) { } println(\"after\");",
         U"if (input()) { }\nprintln(\"after\");\n",
         U"after\n"},
        {U"if (input()) const x=1;\nprintln(\"after\");",
         U"",
         U"if (input()) { } println(\"after\");",
         U"if (input()) { }\nprintln(\"after\");\n",
         U"after\n"},
        {U"if (input()) var a=println(1), b=input();\nprintln(b);",
         U"",
         U"if (input()) var a = println(1), b = input(); println(b);",
         U"if (input()) \n    var a = println(1), b = input();\nprintln(b);\n",
         U"null\n"},
        {U"if (input()) const a=println(1), b=input();\nprintln(b);",
         U"",
         U"if (input()) const a = println(1), b = input(); println(b);",
         U"if (input()) \n    const a = println(1), b = input();\nprintln(b);\n",
         U"null\n"},
        {U"if (input()) var a=println(1), b=input();\nprintln(b);",
         U"x\nhello\n",
         U"if (input()) var a = println(1), b = input(); println(b);",
         U"if (input()) \n    var a = println(1), b = input();\nprintln(b);\n",
         U"1\nhello\n"},
        {U"if (input()) const a=println(1), b=input();\nprintln(b);",
         U"x\nhello\n",
         U"if (input()) const a = println(1), b = input(); println(b);",
         U"if (input()) \n    const a = println(1), b = input();\nprintln(b);\n",
         U"1\nhello\n"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        string_value_t output = run_captured_program(cases[i].source, cases[i].input);
        ASSERT(output.data && !STRCMP(output.data, cases[i].output));
        FREE_STRING(output);

        arena_t *arena = create_arena(16);
        parser_memory_t memory = {arena, arena, arena, arena};
        node_t *root = parse_analysis_test_program(
            &memory,
            (string_value_t){cases[i].source, STRLEN(cases[i].source), false});
        ASSERT(root);
        options_t *options = create_options();
        ASSERT(!analyze(root, &memory, options, NULL));

        string_value_t single = generate_goat_code_from_node(root);
        ASSERT(!STRCMP(single.data, cases[i].single));
        FREE_STRING(single);

        source_builder_t *builder = create_source_builder();
        generate_indented_goat_code_from_node(root, builder, 0);
        string_value_t indented = build_source(builder);
        ASSERT(!STRCMP(indented.data, cases[i].indented));
        FREE_STRING(indented);
        destroy_source_builder(builder);

        const gchar_t *forms[] = {cases[i].single, cases[i].indented};
        for (size_t f = 0; f < 2; f++) {
            string_value_t again = run_captured_program(forms[f], cases[i].input);
            ASSERT(again.data && !STRCMP(again.data, cases[i].output));
            FREE_STRING(again);
        }
        destroy_options(options);
        destroy_arena(arena);
    }
    return true;
}
