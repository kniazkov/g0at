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

/**
 * @brief Creates a test bytecode sequence.
 * @param list Array of instructions to be included in the bytecode.
 * @param count The number of instructions in the list.
 * @return A pointer to the generated bytecode.
 */
static bytecode_t *create_test_bytecode(instruction_t *list, int count) {
    code_builder_t *code_builder = create_code_builder();
    data_builder_t *data_builder = create_data_builder();
    for (int i = 0; i < count; i++) {
        add_instruction(code_builder, list[i]);
    }
    bytecode_t *code = link_code_and_data(code_builder, data_builder);
    destroy_code_builder(code_builder);
    destroy_data_builder(data_builder);
    return code;
}

bool test_boolean_object() {
    object_t *obj1 = get_boolean_object(true);
    string_value_t value = obj1->vtbl->to_string(obj1);
    ASSERT(wcscmp(L"true", value.data) == 0);
    ASSERT(value.should_free == false);
    object_t *obj2 = get_boolean_object(false);
    value = obj2->vtbl->to_string(obj1);
    ASSERT(wcscmp(L"false", value.data) == 0);
    ASSERT(value.should_free == false);
    return true;
}

bool test_integer_object() {
    process_t *proc = create_process();
    object_t *obj = create_integer_object(proc, -1024);
    string_value_t value = obj->vtbl->to_string(obj);
    ASSERT(wcscmp(L"-1024", value.data) == 0);
    ASSERT(value.should_free);
    FREE(value.data);
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
    int_value_t int_val = result->vtbl->get_integer_value(result);
    ASSERT(int_val.has_value);
    ASSERT(int_val.value == 5);
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
    int_value_t int_val = result->vtbl->get_integer_value(result);
    ASSERT(int_val.has_value);
    ASSERT(int_val.value == 9999999999);
    destroy_process(proc);
    free_bytecode(code);
    return true;
}

bool test_strings_concatenation() {
    data_builder_t *data_builder = create_data_builder();
    uint32_t first = add_string_to_data_segment(data_builder, L"it");
    uint32_t second = add_string_to_data_segment(data_builder, L" ");
    uint32_t third = add_string_to_data_segment(data_builder, L"works.");
    code_builder_t *code_bulder = create_code_builder();
    add_instruction(code_bulder, (instruction_t){ .opcode = SLOAD, .arg1 = third });
    add_instruction(code_bulder, (instruction_t){ .opcode = SLOAD, .arg1 = second });
    add_instruction(code_bulder, (instruction_t){ .opcode = SLOAD, .arg1 = first });
    add_instruction(code_bulder, (instruction_t){ .opcode = ADD } );
    add_instruction(code_bulder, (instruction_t){ .opcode = ADD } );
    add_instruction(code_bulder, (instruction_t){ .opcode = END } );
    bytecode_t *code = link_code_and_data(code_bulder, data_builder);
    destroy_code_builder(code_bulder);
    destroy_data_builder(data_builder);
    process_t *proc = create_process();
    run(proc, code);
    ASSERT(proc->main_thread->data_stack->size == 1);
    object_t *result = peek_object_from_stack(proc->main_thread->data_stack, 0);
    string_value_t value = result->vtbl->to_string(result);
    ASSERT(value.data != NULL);
    ASSERT(wcscmp(value.data, L"it works.") == 0);
    ASSERT(value.length == 9);
    destroy_process(proc);
    free_bytecode(code);
    return true;
}

bool test_properties() {
    process_t *process = create_process();
    object_t *obj = create_user_defined_object(process);
    obj->vtbl->set_property(
        obj,
        create_dynamic_string_object(process, STATIC_STRING(L"first")),
        create_dynamic_string_object(process, STATIC_STRING(L"one"))
    );
    obj->vtbl->set_property(
        obj,
        create_dynamic_string_object(process, STATIC_STRING(L"second")),
        create_dynamic_string_object(process, STATIC_STRING(L"two"))
    );
    obj->vtbl->set_property(
        obj,
        create_integer_object(process, 3),
        create_dynamic_string_object(process, STATIC_STRING(L"three"))
    );
    obj->vtbl->set_property(
        obj,
        get_boolean_object(true),
        create_dynamic_string_object(process, STATIC_STRING(L"boolean"))
    );
    object_t *clone = obj->vtbl->clone(process, obj);
    const wchar_t *expected = L"{true=\"boolean\";3=\"three\";\"first\"=\"one\";\"second\"=\"two\"}";
    string_value_t str = obj->vtbl->to_string(obj);
    ASSERT(0 == wcscmp(str.data, expected));
    if (str.should_free) FREE(str.data);
    str = clone->vtbl->to_string(clone);
    ASSERT(0 == wcscmp(str.data, expected));
    if (str.should_free) FREE(str.data);
    object_t *key = create_dynamic_string_object(process, STATIC_STRING(L"first"));
    object_t *value = obj->vtbl->get_property(obj, key);
    ASSERT(value != NULL);
    str = value->vtbl->to_string(value);
    ASSERT(0 == wcscmp(str.data, L"one"));
    if (str.should_free) FREE(str.data);
    key = create_dynamic_string_object(process, STATIC_STRING(L"length"));
    value = value->vtbl->get_property(value, key);
    ASSERT(value != NULL);
    ASSERT(value->vtbl->get_integer_value(value).value == 3);
    key = create_dynamic_string_object(process, STATIC_STRING(L"third"));
    value = obj->vtbl->get_property(obj, key);
    ASSERT(value == NULL);
    object_array_t keys = obj->vtbl->get_keys(obj);
    ASSERT(keys.size == 4);
    for (size_t i = 0; i < keys.size; i++) {
        ASSERT(clone->vtbl->get_property(clone, keys.items[i]) != NULL);
    }
    destroy_process(process);
    return true;
}

bool test_string_topology() {
    process_t *process = create_process();
    object_t *obj = create_dynamic_string_object(process, STATIC_STRING(L"test"));
    object_array_t topology = obj->vtbl->get_topology(obj);
    ASSERT(topology.size = 2);
    const wchar_t *expected = L"{\"length\"=0}";
    string_value_t str = topology.items[0]->vtbl->to_string(topology.items[0]);
    ASSERT(0 == wcscmp(str.data, expected));
    if (str.should_free) FREE(str.data);
    destroy_process(process);
    return true;    
}
