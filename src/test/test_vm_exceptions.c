/** @file test_vm_exceptions.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Context-based VM exception dispatch and ownership.
 */
#include <stdio.h>
#include "test_macro.h"
#include "model/object.h"
#include "model/thread.h"
#include "model/context.h"
#include "model/process.h"
#include "codegen/linker.h"
#include "vm/vm.h"
#include "vm/gc.h"
#include "lib/allocate.h"

static bytecode_t *make_code(const instruction_t *instructions, size_t count) {
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    add_string_to_data_segment(data, L"value");
    for (size_t i = 0; i < count; i++) add_instruction(builder, instructions[i]);
    bytecode_t *code = link_code_and_data(builder, data);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    return code;
}

#define CODE(list) make_code(list, sizeof(list) / sizeof(*list))

bool test_vm_restore(void) {
    instruction_t list[] = {
        {.opcode=ENTER}, {.opcode=ILOAD32,.arg1=7}, {.opcode=RESTORE},
        {.opcode=TRY,.arg1=6}, {.opcode=RESTORE}, {.opcode=END}, {.opcode=THROW}
    };
    bytecode_t *code = CODE(list);
    process_t *proc = create_process();
    context_t *initial = proc->main_thread->context;
    ASSERT(run(proc, code) == 0);
    ASSERT(proc->main_thread->context == initial);
    ASSERT(proc->main_thread->data_stack->size == 1);
    ASSERT(get_object_integer_value(peek_object_from_stack(proc->main_thread->data_stack, 0)).value == 7);
    string_value_t text = bytecode_to_text(code);
    ASSERT(wcsstr(text.data, L"RESTORE") && wcsstr(text.data, L"TRY") && wcsstr(text.data, L"THROW"));
    ASSERT(wcsstr(text.data, L"6"));
    FREE_STRING(text);
    free_bytecode(code);
    destroy_process(proc);
    return true;
}

bool test_vm_throw_values(void) {
    /* Values remain identical across nested context and temporary-stack cleanup. */
    for (int kind = 0; kind < 6; kind++) {
        instruction_t list[] = {
            {.opcode=ILOAD32,.arg1=17}, {.opcode=TRY,.arg1=6}, {.opcode=ENTER},
            {.opcode=ILOAD32,.arg1=9000}, {.opcode=VLOAD,.arg1=0}, {.opcode=THROW},
            {.opcode=END}
        };
        bytecode_t *code = CODE(list);
        process_t *proc = create_process();
        object_t *root = get_root_object();
        object_t *value = kind == 0 ? get_null_object() : kind == 1 ? get_boolean_object(false) :
            kind == 2 ? create_integer_object(proc, 5000) : kind == 3 ? create_real_number_object(proc, 1.5) :
            kind == 4 ? create_string_object(proc, (string_value_t){L"arbitrary",9,false}) :
            create_user_defined_object(proc, (object_array_t){&root,1});
        context_t *initial = proc->main_thread->context;
        object_t *key = create_string_object(proc, (string_value_t){L"value",5,false});
        ASSERT(create_object_property(initial->data, key, value, false) == MSTAT_OK);
        DECREF(key); DECREF(value);
        ASSERT(run(proc, code) == 0);
        ASSERT(proc->main_thread->context == initial);
        ASSERT(proc->main_thread->data_stack->size == 2);
        ASSERT(peek_object_from_stack(proc->main_thread->data_stack, 0) == value);
        ASSERT(get_object_integer_value(peek_object_from_stack(proc->main_thread->data_stack, 1)).value == 17);
        ASSERT(!proc->main_thread->exception.value);
        free_bytecode(code);
        destroy_process(proc);
    }
    return true;
}

bool test_vm_throw_nested(void) {
    instruction_t list[] = {
        {.opcode=TRY,.arg1=8}, {.opcode=ILOAD32,.arg1=123},
        {.opcode=TRY,.arg1=6}, {.opcode=ENTER}, {.opcode=NIL}, {.opcode=THROW},
        {.opcode=THROW}, {.opcode=END}, {.opcode=END}
    };
    bytecode_t *code = CODE(list);
    process_t *proc = create_process();
    context_t *initial = proc->main_thread->context;
    ASSERT(run(proc, code) == 0);
    ASSERT(proc->main_thread->instr_id == 8);
    ASSERT(proc->main_thread->context == initial);
    ASSERT(proc->main_thread->data_stack->size == 1);
    ASSERT(peek_object_from_stack(proc->main_thread->data_stack, 0) == get_null_object());
    free_bytecode(code);
    destroy_process(proc);
    return true;
}

bool test_vm_throw_calls(void) {
    /* A throw crosses a function boundary; RET crosses a TRY without activating it. */
    for (int returning = 0; returning < 2; returning++) {
        instruction_t list[] = {
            {.opcode=TRY,.arg1=7}, {.opcode=ARG,.arg1=9}, {.opcode=FUNC}, {.opcode=CALL},
            {.opcode=RESTORE}, {.opcode=END}, {.opcode=END}, {.opcode=END}, {.opcode=END},
            {.opcode=TRY,.arg1=15}, {.opcode=ENTER}, {.opcode=ILOAD32,.arg1=5000},
            {.opcode=RET}, {.opcode=END}, {.opcode=END}, {.opcode=THROW}
        };
        if (!returning) list[12].opcode = THROW;
        bytecode_t *code = CODE(list);
        process_t *proc = create_process();
        context_t *initial = proc->main_thread->context;
        ASSERT(run(proc, code) == 0);
        ASSERT(proc->main_thread->instr_id == (returning ? 5 : 7));
        ASSERT(proc->main_thread->context == initial);
        ASSERT(proc->main_thread->data_stack->size == 1);
        ASSERT(get_object_integer_value(peek_object_from_stack(proc->main_thread->data_stack, 0)).value == 5000);
        free_bytecode(code);
        destroy_process(proc);
    }
    return true;
}

bool test_vm_throw_uncaught(void) {
    /* A block result survives both caught and uncaught unwinding, including run()'s GC. */
    for (int caught = 0; caught < 2; caught++) {
        instruction_t list[] = {{.opcode=TRY,.arg1=5}, {.opcode=RESTORE}, {.opcode=ENTER},
            {.opcode=LEAVE}, {.opcode=THROW}, {.opcode=END}};
        if (caught) list[1].opcode = NOP;
        bytecode_t *code = CODE(list);
        process_t *proc = create_process();
        context_t *initial = proc->main_thread->context;
        ASSERT((run(proc, code) == 0) == caught);
        ASSERT(proc->main_thread->context == (caught ? initial : get_root_context()));
        ASSERT(proc->main_thread->data_stack->size == (size_t)caught);
        object_t *value = caught ? peek_object_from_stack(proc->main_thread->data_stack, 0)
            : proc->main_thread->exception.value;
        ASSERT(value && value->process == proc);
        collect_garbage(proc);
        ASSERT(get_object_keys(value).size == 0);
        free_bytecode(code);
        destroy_process(proc);
    }
    /* Goat null is an exception value, not absence of an exception. */
    instruction_t list[] = {{.opcode=NIL}, {.opcode=THROW}, {.opcode=END}};
    bytecode_t *code = CODE(list);
    process_t *proc = create_process();
    ASSERT(run(proc, code) != 0);
    ASSERT(proc->main_thread->exception.value == get_null_object());
    free_bytecode(code);
    destroy_process(proc);
    return true;
}

static size_t released_temporaries;

static void count_temporary_release(object_t *object) {
    released_temporaries++;
}

bool test_vm_throw_cleanup(void) {
    instruction_t list[] = {{.opcode=TRY,.arg1=3}, {.opcode=END}, {.opcode=THROW}, {.opcode=END}};
    bytecode_t *code = CODE(list);
    process_t *proc = create_process();
    thread_t *thread = proc->main_thread;
    context_t *initial = thread->context;
    push_object_onto_stack(thread->data_stack, get_boolean_object(true));
    ASSERT(run(proc, code) == 0);
    object_vtbl_t vtbl = *get_boolean_object(false)->vtbl;
    vtbl.dec_ref = count_temporary_release;
    object_t temporary = *get_boolean_object(false);
    temporary.vtbl = &vtbl;
    released_temporaries = 0;
    push_object_onto_stack(thread->data_stack, &temporary);
    push_object_onto_stack(thread->data_stack, &temporary);
    object_t *exception = get_exception_invalid_argument();
    push_object_onto_stack(thread->data_stack, exception);
    thread->args_count = 1;
    thread->args[0] = 99;
    thread->instr_id = 2;
    ASSERT(run(proc, code) == 0);
    ASSERT(thread->context == initial);
    ASSERT(thread->args_count == 0);
    ASSERT(released_temporaries == 2);
    ASSERT(thread->data_stack->size == 2);
    ASSERT(peek_object_from_stack(thread->data_stack, 0) == exception);
    ASSERT(peek_object_from_stack(thread->data_stack, 1) == get_boolean_object(true));
    free_bytecode(code);
    destroy_process(proc);
    return true;
}

bool test_vm_exception_invalid_bytecode(void) {
    instruction_t cases[][3] = {
        {{.opcode=THROW}, {.opcode=END}, {.opcode=END}},
        {{.opcode=TRY,.arg1=3}, {.opcode=END}, {.opcode=END}},
        {{.opcode=RESTORE}, {.opcode=RESTORE}, {.opcode=END}}
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(*cases); i++) {
        bytecode_t *code = CODE(cases[i]);
        process_t *proc = create_process();
        ASSERT(run(proc, code) != 0);
        ASSERT(!proc->main_thread->exception.value);
        free_bytecode(code);
        destroy_process(proc);
    }
    return true;
}
