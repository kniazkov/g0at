/** @file test_finally.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Cleanup grammar, persistence and GC regressions.
 */
#include "test_finally.h"

#include "analysis_test_support.h"
#include "codegen/linker.h"
#include "lib/allocate.h"
#include "lib/file_system.h"
#include "model/context.h"
#include "model/process.h"
#include "model/thread.h"
#include "test_macro.h"
#include "vm/binary.h"
#include "vm/gc.h"
#include "vm/vm.h"

#include <stdio.h>

bool test_finally_parser(void) {
    const gchar_t *valid[] = {U"try {} finally {}",
                              U"try f(); finally g();",
                              U"try throw 1; catch(e) println(e); finally g();",
                              U"try ; finally ;",
                              U"try {} catch(e);",
                              U"try f(); catch(e) x=e;",
                              U"try f(); finally x=1;",
                              U"try if(x) f(); else g(); finally h();",
                              U"try f(); catch(e) if(x) g(); else h(); finally h();",
                              U"do try f(); finally if(x) g(); else h(); while(x);",
                              U"do try f(); catch(e) while(x) break; finally h(); while(x);",
                              U"try try f(); finally g(); finally h();",
                              U"while(x) try break; finally continue;",
                              U"try f(); finally {const f=func(){return 1;};}",
                              U"try return 1; finally return 2;",
                              U"try {} catch(e) var x=1; finally var x=2;"};
    const gchar_t *invalid[] = {U"finally {}",
                                U"try {} finally",
                                U"try finally {}",
                                U"try {} finally {} catch(e) {}",
                                U"try {} finally {} finally {}",
                                U"try {} catch(e) finally {}",
                                U"try {} catch(e) {} catch(e) {}",
                                U"try {} finally (a,b) {}",
                                U"try {} finally break;",
                                U"while(x) try {} finally {const f=func(){continue;};}",
                                U"try ;; finally {}"};
    for (size_t pass = 0; pass < 2; pass++) {
        const gchar_t **texts = pass ? invalid : valid;
        size_t count = pass ? sizeof(invalid) / sizeof(*invalid) : sizeof(valid) / sizeof(*valid);
        for (size_t i = 0; i < count; i++) {
            arena_t *arena = create_arena(8);
            parser_memory_t memory = {arena, arena, arena, arena};
            node_t *root =
                parse_analysis_test_program(&memory,
                                            (string_value_t){texts[i], STRLEN(texts[i]), false});
            if (pass ? root != NULL : root == NULL)
                FPRINTF(stderr, "finally parser %zu/%zu\n", pass, i);
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

bool test_finally_gc(void) {
    process_t *proc = create_process();
    thread_t *thread = proc->main_thread;
    context_t *cleanup = create_context(proc, thread->context, NULL);
    cleanup->control_flow = FLOW_CLEANUP;
    object_t *text = create_string_object(proc, STATIC_STRING(U"pending value"));
    cleanup->pending_value = create_array_object(proc, (object_array_t){&text, 1});
    DECREF(text);
    /* A nested call/scope must retain pending values of every outer cleanup. */
    thread->context = create_context(proc, cleanup, NULL);
    collect_garbage(proc);
    object_array_t values = get_array_elements(cleanup->pending_value);
    ASSERT(values.size == 1);
    string_value_t value = convert_object_to_string(values.items[0]);
    ASSERT(!STRCMP(value.data, U"pending value"));
    FREE_STRING(value);
    thread->context = destroy_context(thread->context);
    thread->context = destroy_context(thread->context);
    collect_garbage(proc);
    destroy_process(proc);
    return true;
}

bool test_finally_binary(void) {
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    add_instruction(builder, (instruction_t){.opcode = FINALLY_ENTER, .arg1 = 2});
    add_instruction(builder, (instruction_t){.opcode = FINALLY_LEAVE, .arg1 = 3});
    add_instruction(builder, (instruction_t){.opcode = FINALLY_END});
    add_instruction(builder, (instruction_t){.opcode = END});
    bytecode_t *code = link_code_and_data(builder, data);
    const char *path = "goat-finally-unit.gbin";
    ASSERT(save_binary_program(path, code, NULL));
    binary_program_t saved = load_binary_program(path);
    ASSERT(saved.code);
    process_t *proc = create_process();
    ASSERT(run(proc, saved.code) == 0);
    destroy_process(proc);
    destroy_binary_program(&saved);
    for (size_t i = 0; i < 2; i++) {
        uint32_t address = ((instruction_t *)code->instructions)[i].arg1;
        ((instruction_t *)code->instructions)[i].arg1 = code->instructions_count;
        ASSERT(save_binary_program(path, code, NULL));
        saved = load_binary_program(path);
        ASSERT(!saved.code);
        destroy_binary_program(&saved);
        ((instruction_t *)code->instructions)[i].arg1 = address;
    }
    proc = create_process();
    proc->main_thread->instr_id = 2;
    ASSERT(run(proc, code) != 0);
    destroy_process(proc);
    remove(path);
    free_bytecode(code);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    return true;
}
