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
    const goat_char_t *valid[] = {U"for(x in []) {}",
                                  U"for(const x in [1,2,]) println(x);",
                                  U"for(x in a) for(y in b) continue;",
                                  U"for(x in a);",
                                  U"if(x) for(y in a) break; else println(x);",
                                  U"for(x in a) if(x) continue; else break;",
                                  U"do for(x in a) break; while(false);",
                                  U"for(x in {a=1;}) {}",
                                  U"while (false) {}",
                                  U"while (false);",
                                  U"while (x) x--;",
                                  U"do {} while (false)",
                                  U"do {} while (false);",
                                  U"do ; while (false);",
                                  U"do x--; while (x)",
                                  U"while (x) break;",
                                  U"do continue; while(x);",
                                  U"for (;;) {break;}",
                                  U"for (;;) continue;",
                                  U"while(x) { if(x) break; else continue; }",
                                  U"do if(x) break; else continue; while(x);",
                                  U"do while(x) break; while(x);",
                                  U"while(x) do break; while(x);",
                                  U"do do break; while(x); while(x);",
                                  U"if(x) while(x) break; else do continue; while(x);",
                                  U"while(x) { const f=func(){while(x) break;}; break; }",
                                  U"while(x) { try {continue;} catch(e) {break;} }",
                                  U"while(x) { f({break;}); }",
                                  U"do {return 1;} while(x);",
                                  U"for (var i=0; i<4; i++) { while(x) continue; break; }"};
    const goat_char_t *invalid[] = {U"for(var x in []) {}",
                                    U"for(x,y in []) {}",
                                    U"for(x in) {}",
                                    U"for(in []) {}",
                                    U"for(x in [],[]) {}",
                                    U"for(x in []; ) {}",
                                    U"for(x.a in []) {}",
                                    U"for(x[0] in []) {}",
                                    U"for(this in []) {}",
                                    U"for(x in []) { const f=func(){break;}; }",
                                    U"for(x in {continue;}) {}",
                                    U"break;",
                                    U"continue;",
                                    U"if(x) break;",
                                    U"{continue;}",
                                    U"while(x) {const f=func(){break;};}",
                                    U"do {const f=func(){continue;};} while(x);",
                                    U"while",
                                    U"while() {}",
                                    U"while(x)",
                                    U"while x {}",
                                    U"while(x,y) {}",
                                    U"do",
                                    U"do {}",
                                    U"do {} while",
                                    U"do {} while()",
                                    U"do {} while x",
                                    U"while({break;}) {}",
                                    U"for({continue;};true;) {}",
                                    U"while(true) {for(;{break;};) {}}",
                                    U"do {} while({continue;})"};
    for (size_t pass = 0; pass < 2; pass++) {
        const goat_char_t **texts = pass ? invalid : valid;
        size_t count = pass ? sizeof(invalid) / sizeof(*invalid) : sizeof(valid) / sizeof(*valid);
        for (size_t i = 0; i < count; i++) {
            arena_t *arena = create_arena(8);
            parser_memory_t memory = {arena, arena, arena, arena};
            node_t *root = parse_analysis_test_program(
                &memory,
                (string_value_t){texts[i], goat_strlen(texts[i]), false});
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
    code->instructions[2].opcode = ITER_NEXT;
    ASSERT(save_binary_program(path, code, NULL));
    saved = load_binary_program(path);
    ASSERT(!saved.code);
    destroy_binary_program(&saved);
    code->instructions[2].opcode = LOOP_JUMP;
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
