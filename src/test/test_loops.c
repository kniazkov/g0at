/** @file test_loops.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Loop grammar, function boundaries, and checked jump targets.
 */
#include "test_loops.h"

#include "analysis_test_support.h"
#include "codegen/linker.h"
#include "lib/allocate.h"
#include "test_macro.h"
#include "vm/binary.h"
#include "vm/vm.h"

#include <stdio.h>

bool test_loops_parser(void) {
    const wchar_t *valid[] = {L"while (false) {}",
                              L"while (false);",
                              L"while (x) x--;",
                              L"do {} while (false)",
                              L"do {} while (false);",
                              L"do ; while (false);",
                              L"do x--; while (x)",
                              L"while (x) break;",
                              L"do continue; while(x);",
                              L"for (;;) {break;}",
                              L"for (;;) continue;",
                              L"while(x) { if(x) break; else continue; }",
                              L"do if(x) break; else continue; while(x);",
                              L"do while(x) break; while(x);",
                              L"while(x) do break; while(x);",
                              L"do do break; while(x); while(x);",
                              L"if(x) while(x) break; else do continue; while(x);",
                              L"while(x) { const f=func(){while(x) break;}; break; }",
                              L"while(x) { try {continue;} catch(e) {break;} }",
                              L"while(x) { f({break;}); }",
                              L"do {return 1;} while(x);",
                              L"for (var i=0; i<4; i++) { while(x) continue; break; }"};
    const wchar_t *invalid[] = {L"break;",
                                L"continue;",
                                L"if(x) break;",
                                L"{continue;}",
                                L"while(x) {const f=func(){break;};}",
                                L"do {const f=func(){continue;};} while(x);",
                                L"while",
                                L"while() {}",
                                L"while(x)",
                                L"while x {}",
                                L"while(x,y) {}",
                                L"do",
                                L"do {}",
                                L"do {} while",
                                L"do {} while()",
                                L"do {} while x",
                                L"while({break;}) {}",
                                L"for({continue;};true;) {}",
                                L"while(true) {for(;{break;};) {}}",
                                L"do {} while({continue;})"};
    for (size_t pass = 0; pass < 2; pass++) {
        const wchar_t **texts = pass ? invalid : valid;
        size_t count = pass ? sizeof(invalid) / sizeof(*invalid) : sizeof(valid) / sizeof(*valid);
        for (size_t i = 0; i < count; i++) {
            arena_t *arena = create_arena(8);
            parser_memory_t memory = {arena, arena, arena, arena};
            node_t *root =
                parse_analysis_test_program(&memory,
                                            (string_value_t){texts[i], wcslen(texts[i]), false});
            if (pass ? root != NULL : root == NULL)
                fprintf(stderr, "loop parser case %zu/%zu\n", pass, i);
            ASSERT(pass ? root == NULL : root != NULL);
            if (root) {
                string_value_t source = generate_goat_code_from_node(root);
                ASSERT(parse_analysis_test_program(&memory, source));
                FREE_STRING(source);
            }
            destroy_arena(arena);
        }
    }
    return true;
}

bool test_loops_bytecode(void) {
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    add_instruction(builder, (instruction_t){.opcode = LOOP_ENTER});
    add_instruction(builder, (instruction_t){.opcode = ILOAD32, .arg1 = 42});
    add_instruction(builder, (instruction_t){.opcode = LOOP_JUMP, .arg1 = 3});
    add_instruction(builder, (instruction_t){.opcode = RESTORE});
    add_instruction(builder, (instruction_t){.opcode = END});
    bytecode_t *code = link_code_and_data(builder, data);
    const char *path = "goat-loops-unit.gbin";
    ASSERT(save_binary_program(path, code, NULL));
    binary_program_t saved = load_binary_program(path);
    ASSERT(saved.code);
    process_t *proc = create_process();
    ASSERT(run(proc, saved.code) == 0);
    destroy_process(proc);
    destroy_binary_program(&saved);
    code->instructions[2].arg1 = code->instructions_count;
    ASSERT(save_binary_program(path, code, NULL));
    saved = load_binary_program(path);
    ASSERT(!saved.code);
    destroy_binary_program(&saved);
    code->instructions[2].arg1 = 3;
    code->instructions[0].opcode = NOP;
    proc = create_process();
    ASSERT(run(proc, code) != 0);
    destroy_process(proc);
    remove(path);
    free_bytecode(code);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    return true;
}
