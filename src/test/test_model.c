/**
 * @file test_model.c
 * @copyright 2025 Ivan Kniazkov
 * @brief A set of tests for testing object model.
 */

#include <stdio.h>

#include "test_model.h"
#include "test_macro.h"
#include "model/object.h"
#include "model/process.h"
#include "model/thread.h"
#include "codegen/linker.h"
#include "vm/vm.h"
#include "lib/allocate.h"
#include "lib/split64.h"

static bytecode_t *create_test_bytecode(instruction_t *list, int count) {
    code_builder_t *code_bulder = create_code_builder();
    data_builder_t *data_builder = create_data_builder();
    for (int i = 0; i < count; i++) {
        add_instruction(code_bulder, list[i]);
    }
    bytecode_t *code = link_code_and_data(code_bulder, data_builder);
    destroy_code_builder(code_bulder);
    destroy_data_builder(data_builder);
    return code;
}

bool test_integer_object() {
    process_t *proc = create_process();
    object_t *obj = create_integer_object(proc, -1024);
    wchar_t *str = obj->vtbl->to_string(obj);
    ASSERT(wcscmp(L"-1024", str) == 0);
    FREE(str);
    destroy_process(proc);
    return true;
}

bool test_addition_of_two_integers() {

    instruction_t list[] = {
        { .opcode = ILOAD32, .arg1 = 2 },
        { .opcode = ILOAD32, .arg1 = 3 },
        { .opcode = ADD },
        { .opcode = END }
    };
    bytecode_t *code = create_test_bytecode(list, 4);
    process_t *proc = create_process();
    run(proc, code);
    ASSERT(proc->main_thread->data_stack->size == 1);
    object_t *result = peek_object_from_stack(proc->main_thread->data_stack, 0);
    const int64_t *value = result->vtbl->get_integer_value(result);
    ASSERT(value != NULL);
    ASSERT(*value == 5);
    destroy_process(proc);
    free_bytecode(code);
    return true;
}

bool test_subtraction_of_two_integers() {
    split64_t s;
    s.int_value = 10000000000;
    instruction_t list[] = {
        { .opcode = ILOAD32, .arg1 = 1 },
        { .opcode = ARG, .arg1 = s.parts[0] },
        { .opcode = ILOAD64, .arg1 = s.parts[1] },
        { .opcode = SUB },
        { .opcode = END }
    };
    bytecode_t *code = create_test_bytecode(list, 5);
    process_t *proc = create_process();
    run(proc, code);
    ASSERT(proc->main_thread->data_stack->size == 1);
    object_t *result = peek_object_from_stack(proc->main_thread->data_stack, 0);
    const int64_t *value = result->vtbl->get_integer_value(result);
    ASSERT(value != NULL);
    ASSERT(*value == 9999999999);
    destroy_process(proc);
    free_bytecode(code);
    return true;
}
