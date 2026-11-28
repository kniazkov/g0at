/**
 * @file test_model.c
 * @copyright 2026 Ivan Kniazkov
 * @brief A set of tests for testing object model.
 */

#include "test_model.h"

#include "codegen/linker.h"
#include "lib/memory/allocate.h"
#include "lib/numbers/split64.h"
#include "lib/threads/os_thread.h"
#include "model/builtin_function.h"
#include "model/byte_array.h"
#include "model/context.h"
#include "model/gc_refcount.h"
#include "model/native_continuation_descriptor.h"
#include "model/object.h"
#include "model/process.h"
#include "model/string_builder.h"
#include "model/thread.h"
#include "test_macro.h"
#include "vm/gc.h"
#include "vm/gc_admission.h"
#include "vm/gc_stats.h"
#include "vm/vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/** @brief Creates a test bytecode sequence. */
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
    string_value_t value = convert_object_to_string(obj1);
    ASSERT(STRCMP(U"true", value.data) == 0);
    ASSERT(value.should_free == false);
    object_t *obj2 = get_boolean_object(false);
    value = convert_object_to_string(obj2);
    ASSERT(STRCMP(U"false", value.data) == 0);
    ASSERT(value.should_free == false);
    return true;
}

bool test_integer_object() {
    process_t *proc = create_process();
    object_t *obj = create_integer_object(proc, -1024);
    string_value_t value = convert_object_to_string(obj);
    ASSERT(STRCMP(U"-1024", value.data) == 0);
    ASSERT(value.should_free);
    FREE_STRING(value);
    destroy_process(proc);
    return true;
}

bool test_addition_of_two_integers() {
    instruction_t list[] = {{.opcode = ILOAD32, .arg1 = 2},
                            {.opcode = ILOAD32, .arg1 = 3},
                            {.opcode = ADD},
                            {.opcode = END}};
    bytecode_t *code = create_test_bytecode(list, 4);
    process_t *proc = create_process();
    run(proc, code);
    ASSERT(proc->main_thread->data_stack->size == 1);
    object_t *result = peek_object_from_stack(proc->main_thread->data_stack, 0);
    int_value_t int_val = get_object_integer_value(result);
    ASSERT(int_val.has_value);
    ASSERT(int_val.value == 5);
    destroy_process(proc);
    free_bytecode(code);
    return true;
}

bool test_subtraction_of_two_integers() {
    split64_t s;
    s.int_value = 10000000000;
    instruction_t list[] = {{.opcode = ARG, .arg1 = s.parts[0]},
                            {.opcode = ILOAD64, .arg1 = s.parts[1]},
                            {.opcode = ILOAD32, .arg1 = 1},
                            {.opcode = SUB},
                            {.opcode = END}};
    bytecode_t *code = create_test_bytecode(list, 5);
    process_t *proc = create_process();
    run(proc, code);
    ASSERT(proc->main_thread->data_stack->size == 1);
    object_t *result = peek_object_from_stack(proc->main_thread->data_stack, 0);
    int_value_t int_val = get_object_integer_value(result);
    ASSERT(int_val.has_value);
    ASSERT(int_val.value == 9999999999);
    destroy_process(proc);
    free_bytecode(code);
    return true;
}

bool test_strings_concatenation() {
    data_builder_t *data_builder = create_data_builder();
    uint32_t first = add_string_to_data_segment(data_builder, U"it");
    uint32_t second = add_string_to_data_segment(data_builder, U" ");
    uint32_t third = add_string_to_data_segment(data_builder, U"works.");
    code_builder_t *code_bulder = create_code_builder();
    add_instruction(code_bulder, (instruction_t){.opcode = SLOAD, .arg1 = first});
    add_instruction(code_bulder, (instruction_t){.opcode = SLOAD, .arg1 = second});
    add_instruction(code_bulder, (instruction_t){.opcode = SLOAD, .arg1 = third});
    add_instruction(code_bulder, (instruction_t){.opcode = ADD});
    add_instruction(code_bulder, (instruction_t){.opcode = ADD});
    add_instruction(code_bulder, (instruction_t){.opcode = END});
    bytecode_t *code = link_code_and_data(code_bulder, data_builder);
    destroy_code_builder(code_bulder);
    destroy_data_builder(data_builder);
    process_t *proc = create_process();
    run(proc, code);
    ASSERT(proc->main_thread->data_stack->size == 1);
    object_t *result = peek_object_from_stack(proc->main_thread->data_stack, 0);
    string_value_t value = convert_object_to_string(result);
    ASSERT(value.data != NULL);
    ASSERT(STRCMP(value.data, U"it works.") == 0);
    ASSERT(value.length == 9);
    destroy_process(proc);
    free_bytecode(code);
    return true;
}

bool test_properties() {
    process_t *process = create_process();
    object_t *root_object = get_root_object();
    object_t *obj = create_user_defined_object(process, (object_array_t){&root_object, 1});
    create_object_property(obj,
                           create_string_object(process, STATIC_STRING(U"first")),
                           create_string_object(process, STATIC_STRING(U"one")),
                           true);
    create_object_property(obj,
                           create_string_object(process, STATIC_STRING(U"second")),
                           create_string_object(process, STATIC_STRING(U"two")),
                           true);
    create_object_property(obj,
                           create_integer_object(process, 3),
                           create_string_object(process, STATIC_STRING(U"three")),
                           true);
    create_object_property(obj,
                           get_boolean_object(true),
                           create_string_object(process, STATIC_STRING(U"boolean")),
                           true);
    object_t *clone = clone_object(process, obj);
    const gchar_t *expected =
        U"{true:\"boolean\",3:\"three\",\"first\":\"one\",\"second\":\"two\"}";
    string_value_t str = convert_object_to_string(obj);
    ASSERT(0 == STRCMP(str.data, expected));
    FREE_STRING(str);
    str = convert_object_to_string(clone);
    ASSERT(0 == STRCMP(str.data, expected));
    FREE_STRING(str);
    object_t *key = create_string_object(process, STATIC_STRING(U"first"));
    object_t *value = get_object_property(obj, key);
    ASSERT(value != NULL);
    str = convert_object_to_string(value);
    ASSERT(0 == STRCMP(str.data, U"one"));
    FREE_STRING(str);
    key = create_string_object(process, STATIC_STRING(U"length"));
    value = get_object_property(value, key);
    ASSERT(value != NULL);
    ASSERT(get_object_integer_value(value).value == 3);
    key = create_string_object(process, STATIC_STRING(U"third"));
    value = get_object_property(obj, key);
    ASSERT(value == NULL);
    object_array_t keys = get_object_keys(obj);
    ASSERT(keys.size == 4);
    for (size_t index = 0; index < keys.size; index++) {
        ASSERT(get_object_property(clone, keys.items[index]) != NULL);
    }
    destroy_process(process);
    return true;
}

bool test_string_topology() {
    process_t *process = create_process();
    object_t *obj = create_string_object(process, STATIC_STRING(U"test"));
    object_array_t topology = get_object_topology(obj);
    ASSERT(topology.size == 2);
    const gchar_t *expected = U"{\"length\"=0;\"trim\"=trim;\"charAt\"=charAt;"
                              U"\"substring\"=substring;\"contains\"=contains;"
                              U"\"indexOf\"=indexOf;\"lastIndexOf\"=lastIndexOf;"
                              U"\"startsWith\"=startsWith;\"endsWith\"=endsWith;"
                              U"\"isEmpty\"=isEmpty;\"isBlank\"=isBlank;"
                              U"\"toUpperCase\"=toUpperCase;\"toLowerCase\"=toLowerCase;"
                              U"\"replace\"=replace;\"repeat\"=repeat;\"format\"=format;"
                              U"\"toUtf8\"=String.toUtf8;\"fromUtf8\"=String.fromUtf8}";
    string_value_t str = convert_object_to_string(topology.items[0]);
    ASSERT(0 == STRCMP(str.data, expected));
    FREE_STRING(str);
    destroy_process(process);
    return true;
}

bool test_store_and_load() {
    data_builder_t *data_builder = create_data_builder();
    uint32_t name_idx = add_string_to_data_segment(data_builder, U"value");
    code_builder_t *code_bulder = create_code_builder();
    add_instruction(code_bulder, (instruction_t){.opcode = ILOAD32, .arg1 = 1024});
    add_instruction(code_bulder, (instruction_t){.opcode = STORE, .arg1 = name_idx});
    add_instruction(code_bulder, (instruction_t){.opcode = POP});
    add_instruction(code_bulder, (instruction_t){.opcode = VLOAD, .arg1 = name_idx});
    add_instruction(code_bulder, (instruction_t){.opcode = END});
    bytecode_t *code = link_code_and_data(code_bulder, data_builder);
    destroy_code_builder(code_bulder);
    destroy_data_builder(data_builder);
    process_t *proc = create_process();
    run(proc, code);
    ASSERT(proc->main_thread->data_stack->size == 1);
    object_t *result = peek_object_from_stack(proc->main_thread->data_stack, 0);
    int_value_t int_val = get_object_integer_value(result);
    ASSERT(int_val.has_value);
    ASSERT(int_val.value == 1024);
    destroy_process(proc);
    free_bytecode(code);
    return true;
}

/** @brief Tests the built-in `trim` method and function on strings. */
bool test_trim_function() {
    process_t *proc = create_process();
    thread_t *thread = proc->main_thread;
    object_t *function = get_function_trim();

    object_t *key = create_string_object(proc, STATIC_STRING(U"trim"));
    ASSERT(get_object_property(get_string_proto(), key) == function);
    object_t *str = create_string_object(proc, STATIC_STRING(U"   abcde   "));
    DECREF(key);

    ASSERT(call_object(function, 0, str, thread));
    ASSERT(thread->data_stack->size == 1);
    object_t *result = pop_object_from_stack(thread->data_stack);
    string_value_t value = convert_object_to_string(result);
    ASSERT(STRCMP(value.data, U"abcde") == 0 && value.length == 5);
    FREE_STRING(value);
    DECREF(result);

    push_object_onto_stack(thread->data_stack, get_static_integer_object(42));
    ASSERT(!call_object(function, 1, NULL, thread));
    ASSERT(thread->exception.value == get_exception_invalid_operation());
    clear_exception(&thread->exception);

    ASSERT(!call_object(function, 0, NULL, thread));
    ASSERT(thread->exception.value == get_exception_invalid_argument());
    clear_exception(&thread->exception);

    DECREF(str);
    destroy_process(proc);
    return true;
}

/** @brief Tests the built-in `instanceOf` method and function and the named prototypes. */
bool test_instance_of_function() {
    process_t *proc = create_process();
    thread_t *thread = proc->main_thread;
    object_t *function = get_function_instance_of();

    object_t *key = create_string_object(proc, STATIC_STRING(U"instanceOf"));
    ASSERT(get_object_property(get_root_object(), key) == function);
    DECREF(key);

    static const struct {
        const gchar_t *name;
        object_t *(*getter)(void);
    } types[] = {
        {U"Array", get_array_proto},
        {U"Boolean", get_boolean_proto},
        {U"ByteArray", get_byte_array_proto},
        {U"Character", get_character_proto},
        {U"Function", get_function_proto},
        {U"Integer", get_integer_proto},
        {U"Null", get_null_object},
        {U"Number", get_numeric_proto},
        {U"Object", get_root_object},
        {U"Real", get_real_proto},
        {U"String", get_string_proto},
        {U"StringBuilder", get_string_builder_proto},
    };

    for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++) {
        key = create_string_object(proc,
                                   (string_value_t){types[i].name, STRLEN(types[i].name), false});
        ASSERT(get_object_property(get_root_context()->data, key) == types[i].getter());
        DECREF(key);
    }

    object_t *str = create_string_object(proc, STATIC_STRING(U"abs"));
    push_object_onto_stack(thread->data_stack, get_string_proto());
    ASSERT(call_object(function, 1, str, thread));
    object_t *result = pop_object_from_stack(thread->data_stack);
    ASSERT(get_object_boolean_value(result));
    DECREF(result);

    push_object_onto_stack(thread->data_stack, get_integer_proto());
    ASSERT(call_object(function, 1, str, thread));
    result = pop_object_from_stack(thread->data_stack);
    ASSERT(!get_object_boolean_value(result));
    DECREF(result);

    push_object_onto_stack(thread->data_stack, get_root_object());
    ASSERT(call_object(function, 1, str, thread));
    result = pop_object_from_stack(thread->data_stack);
    ASSERT(get_object_boolean_value(result));
    DECREF(result);
    DECREF(str);

    object_t *num = create_integer_object(proc, 5);
    push_object_onto_stack(thread->data_stack, get_integer_proto());
    ASSERT(call_object(function, 1, num, thread));
    result = pop_object_from_stack(thread->data_stack);
    ASSERT(get_object_boolean_value(result));
    DECREF(result);

    push_object_onto_stack(thread->data_stack, get_numeric_proto());
    ASSERT(call_object(function, 1, num, thread));
    result = pop_object_from_stack(thread->data_stack);
    ASSERT(get_object_boolean_value(result));
    DECREF(result);

    push_object_onto_stack(thread->data_stack, get_real_proto());
    ASSERT(call_object(function, 1, num, thread));
    result = pop_object_from_stack(thread->data_stack);
    ASSERT(!get_object_boolean_value(result));
    DECREF(result);
    DECREF(num);

    push_object_onto_stack(thread->data_stack, get_null_object());
    ASSERT(call_object(function, 1, get_null_object(), thread));
    result = pop_object_from_stack(thread->data_stack);
    ASSERT(get_object_boolean_value(result));
    DECREF(result);

    push_object_onto_stack(thread->data_stack, get_root_object());
    ASSERT(call_object(function, 1, get_null_object(), thread));
    result = pop_object_from_stack(thread->data_stack);
    ASSERT(get_object_boolean_value(result));
    DECREF(result);

    object_t *root = get_root_object();
    object_t *obj = create_user_defined_object(proc, (object_array_t){&root, 1});
    push_object_onto_stack(thread->data_stack, get_root_object());
    ASSERT(call_object(function, 1, obj, thread));
    result = pop_object_from_stack(thread->data_stack);
    ASSERT(get_object_boolean_value(result));
    DECREF(result);

    push_object_onto_stack(thread->data_stack, get_string_proto());
    ASSERT(call_object(function, 1, obj, thread));
    result = pop_object_from_stack(thread->data_stack);
    ASSERT(!get_object_boolean_value(result));
    DECREF(result);

    INCREF(obj);
    push_object_onto_stack(thread->data_stack, obj);
    push_object_onto_stack(thread->data_stack, get_root_object());
    ASSERT(call_object(function, 2, NULL, thread));
    result = pop_object_from_stack(thread->data_stack);
    ASSERT(get_object_boolean_value(result));
    DECREF(result);

    INCREF(obj);
    push_object_onto_stack(thread->data_stack, obj);
    push_object_onto_stack(thread->data_stack, get_string_proto());
    ASSERT(call_object(function, 2, NULL, thread));
    result = pop_object_from_stack(thread->data_stack);
    ASSERT(!get_object_boolean_value(result));
    DECREF(result);

    push_object_onto_stack(thread->data_stack, create_integer_object(proc, 1));
    ASSERT(!call_object(function, 1, NULL, thread));
    ASSERT(thread->exception.value == get_exception_invalid_argument());
    clear_exception(&thread->exception);

    ASSERT(!call_object(function, 0, NULL, thread));
    ASSERT(thread->exception.value == get_exception_invalid_argument());
    clear_exception(&thread->exception);

    ASSERT(!call_object(function, 0, get_root_object(), thread));
    ASSERT(thread->exception.value == get_exception_invalid_argument());
    clear_exception(&thread->exception);

    destroy_process(proc);
    return true;
}

bool test_sign_function() {
    data_builder_t *data_builder = create_data_builder();
    uint32_t name_idx = add_string_to_data_segment(data_builder, U"sign");
    code_builder_t *code_bulder = create_code_builder();
    add_instruction(code_bulder, (instruction_t){.opcode = ILOAD32, .arg1 = -1024});
    add_instruction(code_bulder, (instruction_t){.opcode = VLOAD, .arg1 = name_idx});
    add_instruction(code_bulder, (instruction_t){.opcode = CALL, .arg0 = 1});
    add_instruction(code_bulder, (instruction_t){.opcode = END});
    bytecode_t *code = link_code_and_data(code_bulder, data_builder);
    destroy_code_builder(code_bulder);
    destroy_data_builder(data_builder);
    process_t *proc = create_process();
    run(proc, code);
    ASSERT(proc->main_thread->data_stack->size == 1);
    object_t *result = peek_object_from_stack(proc->main_thread->data_stack, 0);
    int_value_t int_val = get_object_integer_value(result);
    ASSERT(int_val.has_value);
    ASSERT(int_val.value == -1);
    destroy_process(proc);
    free_bytecode(code);
    return true;
}

bool test_context_cloning() {
    data_builder_t *data_builder = create_data_builder();
    uint32_t name_x_idx = add_string_to_data_segment(data_builder, U"x");
    uint32_t name_y_idx = add_string_to_data_segment(data_builder, U"y");
    code_builder_t *code_bulder = create_code_builder();
    add_instruction(code_bulder, (instruction_t){.opcode = ILOAD32, .arg1 = 2});
    add_instruction(code_bulder, (instruction_t){.opcode = VAR, .arg1 = name_x_idx});
    add_instruction(code_bulder, (instruction_t){.opcode = ENTER});
    add_instruction(code_bulder, (instruction_t){.opcode = VLOAD, .arg1 = name_x_idx});
    add_instruction(code_bulder, (instruction_t){.opcode = ILOAD32, .arg1 = 3});
    add_instruction(code_bulder, (instruction_t){.opcode = ADD});
    add_instruction(code_bulder, (instruction_t){.opcode = VAR, .arg1 = name_y_idx});
    add_instruction(code_bulder, (instruction_t){.opcode = LEAVE});
    add_instruction(code_bulder, (instruction_t){.opcode = END});
    bytecode_t *code = link_code_and_data(code_bulder, data_builder);
    destroy_code_builder(code_bulder);
    destroy_data_builder(data_builder);
    process_t *proc = create_process();
    run(proc, code);
    ASSERT(proc->main_thread->data_stack->size == 1);
    object_t *obj = peek_object_from_stack(proc->main_thread->data_stack, 0);
    object_t *key = create_string_object(proc, STATIC_STRING(U"y"));
    object_t *value = get_object_property(obj, key);
    ASSERT(value != NULL);
    string_value_t str = convert_object_to_string(value);
    ASSERT(0 == STRCMP(str.data, U"5"));
    FREE_STRING(str);
    destroy_process(proc);
    free_bytecode(code);
    return true;
}

bool test_function_definition() {
    data_builder_t *data_builder = create_data_builder();
    uint32_t name_a_idx = add_string_to_data_segment(data_builder, U"a");
    uint32_t name_b_idx = add_string_to_data_segment(data_builder, U"b");
    uint32_t arg_names[] = {name_a_idx, name_b_idx};
    uint32_t arg_names_idx = add_data_to_data_segment(data_builder, arg_names, sizeof(arg_names));
    code_builder_t *code_bulder = create_code_builder();
    add_instruction(code_bulder, (instruction_t){.opcode = ILOAD32, .arg1 = 2});
    add_instruction(code_bulder, (instruction_t){.opcode = ILOAD32, .arg1 = 3});
    add_instruction(code_bulder, (instruction_t){.opcode = ARG, .arg1 = 6});
    add_instruction(code_bulder, (instruction_t){.opcode = FUNC, .arg0 = 2, .arg1 = arg_names_idx});
    add_instruction(code_bulder, (instruction_t){.opcode = CALL, .arg0 = 2});
    add_instruction(code_bulder, (instruction_t){.opcode = END});
    add_instruction(code_bulder, (instruction_t){.opcode = VLOAD, .arg1 = name_a_idx});
    add_instruction(code_bulder, (instruction_t){.opcode = VLOAD, .arg1 = name_b_idx});
    add_instruction(code_bulder, (instruction_t){.opcode = ADD});
    add_instruction(code_bulder, (instruction_t){.opcode = RET});
    bytecode_t *code = link_code_and_data(code_bulder, data_builder);
    destroy_code_builder(code_bulder);
    destroy_data_builder(data_builder);
    process_t *proc = create_process();
    run(proc, code);
    ASSERT(proc->main_thread->data_stack->size == 1);
    object_t *result = peek_object_from_stack(proc->main_thread->data_stack, 0);
    int_value_t int_val = get_object_integer_value(result);
    ASSERT(int_val.has_value);
    ASSERT(int_val.value == 5);
    destroy_process(proc);
    free_bytecode(code);
    return true;
}

bool test_closure() {
    data_builder_t *data_builder = create_data_builder();
    uint32_t name_a_idx = add_string_to_data_segment(data_builder, U"a");
    uint32_t name_b_idx = add_string_to_data_segment(data_builder, U"b");
    uint32_t arg_names[] = {name_a_idx};
    uint32_t arg_names_idx = add_data_to_data_segment(data_builder, arg_names, sizeof(arg_names));
    code_builder_t *code_bulder = create_code_builder();
    add_instruction(code_bulder, (instruction_t){.opcode = ILOAD32, .arg1 = 2});
    add_instruction(code_bulder, (instruction_t){.opcode = VAR, .arg1 = name_b_idx});
    add_instruction(code_bulder, (instruction_t){.opcode = ILOAD32, .arg1 = 3});
    add_instruction(code_bulder, (instruction_t){.opcode = ARG, .arg1 = 7});
    add_instruction(code_bulder, (instruction_t){.opcode = FUNC, .arg0 = 1, .arg1 = arg_names_idx});
    add_instruction(code_bulder, (instruction_t){.opcode = CALL, .arg0 = 1});
    add_instruction(code_bulder, (instruction_t){.opcode = END});
    add_instruction(code_bulder, (instruction_t){.opcode = VLOAD, .arg1 = name_a_idx});
    add_instruction(code_bulder, (instruction_t){.opcode = VLOAD, .arg1 = name_b_idx});
    add_instruction(code_bulder, (instruction_t){.opcode = ADD});
    add_instruction(code_bulder, (instruction_t){.opcode = RET});
    bytecode_t *code = link_code_and_data(code_bulder, data_builder);
    destroy_code_builder(code_bulder);
    destroy_data_builder(data_builder);
    process_t *proc = create_process();
    run(proc, code);
    ASSERT(proc->main_thread->data_stack->size == 1);
    object_t *result = peek_object_from_stack(proc->main_thread->data_stack, 0);
    int_value_t int_val = get_object_integer_value(result);
    ASSERT(int_val.has_value);
    ASSERT(int_val.value == 5);
    destroy_process(proc);
    free_bytecode(code);
    return true;
}

bool test_root_object_call() {
    ASSERT(!call_object(get_root_object(), 0, NULL, NULL));
    return true;
}

bool test_set_property_self_reference() {
    process_t *process = create_process();
    object_t *root_object = get_root_object();
    object_t *obj = create_user_defined_object(process, (object_array_t){&root_object, 1});
    object_t *key = create_string_object(process, STATIC_STRING(U"self"));
    /* A self-reference holds no reference count (see create_property), so replacing it must not
       release it and a new self-reference must not be retained. */
    ASSERT(create_object_property(obj, key, obj, false) == MSTAT_OK);
    ASSERT(get_object_property(obj, key) == obj);
    ASSERT(set_object_property(obj, key, get_null_object()) == MSTAT_OK);
    ASSERT(get_object_property(obj, key) == get_null_object());
    ASSERT(set_object_property(obj, key, obj) == MSTAT_OK);
    ASSERT(get_object_property(obj, key) == obj);
    ASSERT(set_object_property(obj, key, obj) == MSTAT_OK);
    ASSERT(get_object_property(obj, key) == obj);
    ASSERT(set_object_property(obj, key, get_null_object()) == MSTAT_OK);
    ASSERT(get_object_property(obj, key) == get_null_object());
    destroy_process(process);
    return true;
}

bool test_context_this_cycle_release() {
    process_t *proc = create_process();
    size_t objects_before = proc->objects.size;
    context_t *ctx = create_context(proc, get_root_context(), NULL);
    object_t *data = ctx->data;
    /* Set up the same self-reference that a method call creates: the `this` property points back
       to the context's data object itself. */
    ASSERT(create_object_property(data, get_this_key(), data, true) == MSTAT_OK);
    destroy_context(ctx);
    /* The `this` property is a weak self-reference, so releasing the context's reference must
       release the data object without leaving a cycle behind. */
    ASSERT(proc->objects.size == objects_before);
    destroy_process(proc);
    return true;
}

bool test_context_this_preserved() {
    process_t *proc = create_process();
    size_t objects_before = proc->objects.size;
    context_t *ctx = create_context(proc, get_root_context(), NULL);
    object_t *data = ctx->data;
    ASSERT(create_object_property(data, get_this_key(), data, true) == MSTAT_OK);
    /* Simulate the data object escaping the call: the program keeps a reference to it. */
    INCREF(data);
    destroy_context(ctx);
    /* Destroying the context must not mutate the still-live object: `this` keeps pointing to it. */
    ASSERT(get_object_property(data, get_this_key()) == data);
    ASSERT(create_object_property(data, get_this_key(), data, true)
           == MSTAT_PROPERTY_ALREADY_EXISTS);
    DECREF(data);
    ASSERT(proc->objects.size == objects_before);
    destroy_process(proc);
    return true;
}

/** @brief Test virtual method with a distinctive result. */
static object_t *custom_clone(process_t *process, object_t *obj) {
    return create_integer_object(process, 12345);
}

bool test_clone_method(void) {
    process_t *proc = create_process();
    object_t *key = create_string_object(proc, STATIC_STRING(U"clone"));
    object_t *method = get_object_property(get_root_object(), key);
    ASSERT(method == get_function_clone());
    object_vtbl_t vtbl = *get_root_object()->vtbl;
    vtbl.clone = custom_clone;
    object_t receiver = {.vtbl = &vtbl};
    ASSERT(call_object(method, 0, &receiver, proc->main_thread));
    object_t *result = pop_object_from_stack(proc->main_thread->data_stack);
    ASSERT(get_object_integer_value(result).value == 12345);
    DECREF(result);
    DECREF(key);
    object_t *values[] = {create_integer_object(proc, 1000),
                          create_real_number_object(proc, 2.5),
                          create_character_object(proc, 0x416),
                          create_string_object(proc, STATIC_STRING(U"dynamic string"))};
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); i++) {
        object_t *copy = clone_object(proc, values[i]);
        ASSERT(copy == values[i]);
        DECREF(values[i]);
        string_value_t text = convert_object_to_string(copy);
        ASSERT(text.length > 0);
        FREE_STRING(text);
        DECREF(copy);
    }
    destroy_process(proc);
    return true;
}

/** @brief Flat copies inherited keys once, drops parents and constness, and preserves values. */
bool test_flat_method(void) {
    process_t *proc = create_process();
    object_t *root = get_root_object();
    object_t *parent = create_user_defined_object(proc, (object_array_t){&root, 1});
    object_t *child = create_user_defined_object(proc, (object_array_t){&parent, 1});
    object_t *key = create_string_object(proc, STATIC_STRING(U"value"));
    object_t *nested = create_array_object(proc, (object_array_t){0});
    ASSERT(create_object_property(parent, key, nested, true) == MSTAT_OK);
    ASSERT(call_object(get_function_flat(), 0, child, proc->main_thread));
    object_t *copy = pop_object_from_stack(proc->main_thread->data_stack);
    ASSERT(copy != child && copy->vtbl->type == TYPE_USER_DEFINED_OBJECT);
    object_array_t parents = get_object_prototypes(copy);
    ASSERT(parents.size == 1 && parents.items[0] == root);
    ASSERT(get_object_keys(copy).size == 1);
    ASSERT(get_object_property(copy, key) == nested);
    ASSERT(set_object_property(copy, key, get_null_object()) == MSTAT_OK);
    ASSERT(get_object_property(parent, key) == nested);
    ASSERT(call_object(get_function_flat(), 0, nested, proc->main_thread));
    object_t *same = pop_object_from_stack(proc->main_thread->data_stack);
    ASSERT(same == nested);
    DECREF(same);
    DECREF(copy);
    DECREF(child);
    DECREF(parent);
    DECREF(key);
    DECREF(nested);
    destroy_process(proc);
    return true;
}

/** @brief Preparation owns its input, survives GC, and is immutable after success. */
bool test_process_launch(void) {
    process_t *proc = create_process();
    ASSERT(!prepare_process_launch(NULL, proc->module, 0, "/app.goat"));
    ASSERT(!prepare_process_launch(proc, NULL, 0, "/app.goat"));
    ASSERT(!prepare_process_launch(proc, proc->module, 0, NULL));
    ASSERT(!prepare_process_launch(proc, proc->module, 0, "app.goat"));
    ASSERT(!prepare_process_launch(proc, proc->module, 0, "/\xff/app.goat"));
    ASSERT(!proc->launch_module && !proc->launch_directory && !proc->launch_file);
    char path[] = "/\xd0\xbf\xd1\x83\xd1\x82\xd1\x8c/\xf0\x9f\x98\x80/app.goat";
    module_t *module = create_module_identity();
    ASSERT(prepare_process_launch(proc, module, 23, path));
    ASSERT(proc->launch_module == module && proc->launch_file == 23);
    release_module_identity(module);
    object_t *directory = proc->launch_directory;
    path[1] = 'x';
    ASSERT(!prepare_process_launch(proc, proc->module, 0, "/other/app.goat"));
    ASSERT(proc->launch_module == module && proc->launch_file == 23);
    ASSERT(proc->launch_directory == directory);
    for (size_t i = 0; i < 3; i++) {
        collect_garbage(proc);
        string_value_t value = convert_object_to_string(directory);
        ASSERT(value.length == 7 && !STRCMP(value.data, U"/путь/😀"));
        FREE_STRING(value);
    }
    destroy_process(proc);
    proc = create_process();
#ifdef _WIN32
    ASSERT(prepare_process_launch(proc, proc->module, 0, "C:/app.goat"));
    string_value_t root = convert_object_to_string(proc->launch_directory);
    ASSERT(root.length == 3 && !STRCMP(root.data, U"C:/"));
#else
    ASSERT(prepare_process_launch(proc, proc->module, 0, "/app.goat"));
    string_value_t root = convert_object_to_string(proc->launch_directory);
    ASSERT(root.length == 1 && !STRCMP(root.data, U"/"));
#endif
    FREE_STRING(root);
    destroy_process(proc);
    return true;
}

/** @brief VLOAD uses exact module/file provenance and the process-wide directory. */
bool test_process_launch_vm(void) {
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    uint32_t main_name = add_string_to_data_segment_ex(data, (string_view_t){U"__main__", 8});
    uint32_t dir_name = add_string_to_data_segment_ex(data, (string_view_t){U"__dir__", 7});
    add_instruction(builder, (instruction_t){.opcode = VLOAD, .arg1 = main_name});
    add_instruction(builder, (instruction_t){.opcode = VLOAD, .arg1 = dir_name});
    add_instruction(builder, (instruction_t){.opcode = END});
    bytecode_t *code = link_code_and_data(builder, data);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    for (size_t mode = 0; mode < 4; mode++) {
        process_t *proc = create_process();
        module_t *foreign = create_module_identity();
        if (mode)
            ASSERT(prepare_process_launch(proc,
                                          mode == 3 ? foreign : proc->module,
                                          mode == 2 ? 1 : 0,
                                          "/program/app.goat"));
        release_module_identity(foreign);
        ASSERT(run(proc, code) == 0);
        ASSERT(proc->main_thread->data_stack->size == 2);
        object_t *directory = pop_object_from_stack(proc->main_thread->data_stack);
        string_value_t value = convert_object_to_string(directory);
        ASSERT(mode ? !STRCMP(value.data, U"/program") : value.length == 0);
        FREE_STRING(value);
        DECREF(directory);
        object_t *main = pop_object_from_stack(proc->main_thread->data_stack);
        ASSERT(main == get_boolean_object(mode == 1));
        DECREF(main);
        collect_garbage(proc);
        destroy_process(proc);
    }
    free_bytecode(code);
    return true;
}

/** @brief Storage growth, retained capacity, snapshots, clone ownership and GC/pool reuse. */
bool test_string_builder_storage(void) {
    process_t *proc = create_process();
    object_t *proto = get_string_builder_proto();
    object_t *obj = create_user_defined_object(proc, (object_array_t){&proto, 1});
    gc_root_handle_t obj_root = register_gc_root(proc, obj);
    object_t *key = create_string_object(proc, STATIC_STRING(U"init"));
    const builtin_function_t *init =
        ((builtin_function_object_t *)get_object_property(proto, key))->descriptor;
    DECREF(key);
    operation_result_t result = init->execute_method(obj, NULL, 0, proc->main_thread);
    ASSERT(!result.is_exception && result.value == obj);
    DECREF(result.value);
    string_builder_t *buffer = get_string_builder_storage(obj);
    ASSERT(buffer && buffer->length == 0);
    key = create_string_object(proc, STATIC_STRING(U"append"));
    const builtin_function_t *append =
        ((builtin_function_object_t *)get_object_property(proto, key))->descriptor;
    DECREF(key);
    object_t *arg = create_string_object(proc, STATIC_STRING(U"abc😀"));
    for (size_t i = 0; i < 1024; i++) {
        result = append->execute_method(obj, &arg, 1, proc->main_thread);
        ASSERT(!result.is_exception && result.value == obj);
        DECREF(result.value);
    }
    DECREF(arg);
    ASSERT(buffer->length == 4096 && buffer->capacity >= 4096);
    size_t capacity = buffer->capacity;
    gchar_t *data = buffer->data;
    string_value_t snapshot = string_builder_snapshot(obj);
    object_t *copy = clone_object(proc, obj);
    gc_root_handle_t copy_root = register_gc_root(proc, copy);
    ASSERT(get_string_builder_storage(copy)->data != data);
    ASSERT(get_string_builder_storage(copy)->length == 4096);
    key = create_string_object(proc, STATIC_STRING(U"clear"));
    const builtin_function_t *clear =
        ((builtin_function_object_t *)get_object_property(proto, key))->descriptor;
    DECREF(key);
    result = clear->execute_method(obj, NULL, 0, proc->main_thread);
    ASSERT(!result.is_exception && result.value == obj);
    DECREF(result.value);
    ASSERT(buffer->length == 0 && buffer->capacity == capacity && buffer->data == data);
    ASSERT(snapshot.length == 4096 && snapshot.data[3] == U'😀');
    collect_garbage(proc);
    ASSERT(get_string_builder_storage(copy)->length == 4096);
    FREE_STRING(snapshot);
    unregister_gc_root(proc, copy_root);
    DECREF(copy);
    unregister_gc_root(proc, obj_root);
    DECREF(obj);
    /* Reusing a pooled ordinary object must not resurrect native storage. */
    object_t *root = get_root_object();
    obj = create_user_defined_object(proc, (object_array_t){&root, 1});
    ASSERT(!get_string_builder_storage(obj));
    DECREF(obj);
    /* A cyclic unreachable builder is reclaimed by tracing, including its native buffer. */
    obj = create_user_defined_object(proc, (object_array_t){&proto, 1});
    result = init->execute_method(obj, NULL, 0, proc->main_thread);
    ASSERT(!result.is_exception);
    DECREF(result.value);
    key = create_string_object(proc, STATIC_STRING(U"cycle"));
    ASSERT(create_object_property(obj, key, obj, false) == MSTAT_OK);
    DECREF(key);
    collect_garbage(proc);
    destroy_process(proc);
    return true;
}

bool test_gc_pending_thread_callable(void) {
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    uint32_t capture_id = add_string_to_data_segment(data, U"captured");
    add_instruction(builder, (instruction_t){.opcode = CALL});
    add_instruction(builder, (instruction_t){.opcode = END});
    add_instruction(builder, (instruction_t){.opcode = VLOAD, .arg1 = capture_id});
    add_instruction(builder, (instruction_t){.opcode = RET});
    bytecode_t *code = link_code_and_data(builder, data);
    destroy_code_builder(builder);
    destroy_data_builder(data);

    process_t *proc = create_process();
    size_t baseline = proc->objects.size;
    thread_t *worker = create_thread(proc, create_context(proc, get_root_context(), NULL));
    worker->program_worker = true;
    object_t *root = get_root_object();
    object_t *closure = create_user_defined_object(proc, (object_array_t){&root, 1});
    object_t *key = create_string_object(proc, STATIC_STRING(U"captured"));
    object_t *captured = create_integer_object(proc, 317);
    create_object_property(closure, key, captured, true);
    DECREF(key);
    DECREF(captured);
    worker->start_function = create_function_object(proc, NULL, 0, 2, closure, NULL, false);
    DECREF(closure);
    size_t before = proc->objects.size;
    collect_garbage(proc);

    /* Check liveness before touching the callable. In a failing build the sweep
       may already have reclaimed it, so teardown must not release the stale slot. */
    bool preserved = proc->objects.size == before;
    if (preserved)
        push_object_onto_stack(proc->main_thread->data_stack, worker->start_function);
    worker->start_function = NULL;
    destroy_thread(worker);

    bool callable = false;
    bool reclaimed = false;
    if (preserved && run(proc, code) == 0 && proc->main_thread->data_stack->size == 1) {
        object_t *result = pop_object_from_stack(proc->main_thread->data_stack);
        int_value_t value = get_object_integer_value(result);
        callable = value.has_value && value.value == 317;
        DECREF(result);
        collect_garbage(proc);
        reclaimed = proc->objects.size == baseline;
    }
    destroy_process(proc);
    free_bytecode(code);
    ASSERT(preserved);
    ASSERT(callable);
    ASSERT(reclaimed);
    return true;
}

bool test_gc_no_registered_threads(void) {
    process_t *proc = create_process();
    destroy_thread(proc->main_thread);
    object_t *root = create_integer_object(proc, 317);
    gc_root_handle_t root_root = register_gc_root(proc, root);
    collect_garbage(proc);
    bool preserved = proc->objects.size == 1;
    unregister_gc_root(proc, root_root);
    if (preserved)
        DECREF(root);
    bool reclaimed = proc->objects.size == 0;
    destroy_process(proc);
    ASSERT(preserved);
    ASSERT(reclaimed);
    return true;
}

/** @brief Checks a dead array cycle with duplicate edges to one independently owned survivor. */
static bool sweep_array_references(bool survivor_first) {
    process_t *proc = create_process();
    size_t baseline = proc->objects.size;
    object_t *survivor = survivor_first ? create_array_object(proc, (object_array_t){0}) : NULL;
    object_t *items[1031];
    for (size_t i = 0; i < sizeof(items) / sizeof(*items); ++i)
        items[i] = get_null_object();
    object_t *a =
        create_array_object(proc, (object_array_t){items, sizeof(items) / sizeof(*items)});
    object_t *b = create_array_object(proc, (object_array_t){items, 2});
    if (!survivor)
        survivor = create_array_object(proc, (object_array_t){0});
    object_t *length = get_object_property(a, get_string_length());
    INCREF(length);
    INCREF(survivor); /* Two independent outside owners; neither is the root table. */
    gc_root_handle_t survivor_root = register_gc_root(proc, survivor);
    gc_root_handle_t length_root = register_gc_root(proc, length);

    object_t *zero = get_integer_zero();
    object_t *one = create_integer_object(proc, 1);
    object_t *two = create_integer_object(proc, 2);
    operation_result_t writes[] = {
        a->vtbl->set_index(proc, a, (object_array_t){&zero, 1}, b),
        a->vtbl->set_index(proc, a, (object_array_t){&one, 1}, survivor),
        a->vtbl->set_index(proc, a, (object_array_t){&two, 1}, survivor),
        b->vtbl->set_index(proc, b, (object_array_t){&zero, 1}, a),
        b->vtbl->set_index(proc, b, (object_array_t){&one, 1}, survivor),
    };
    bool good = true;
    for (size_t i = 0; i < sizeof(writes) / sizeof(*writes); ++i) {
        good = good && !writes[i].is_exception;
        DECREF(writes[i].value);
    }
    DECREF(one);
    DECREF(two);
    DECREF(a);
    DECREF(b);

    collect_garbage(proc);
    collect_garbage(proc);
    good = good && proc->objects.size == baseline + 2;
    unregister_gc_root(proc, length_root);
    DECREF(length);
    good = good && proc->objects.size == baseline + 1;
    DECREF(survivor);
    good = good && proc->objects.size == baseline + 1;
    unregister_gc_root(proc, survivor_root);
    DECREF(survivor);
    good = good && proc->objects.size == baseline;
    destroy_process(proc);
    return good;
}

bool test_gc_sweep_array_references(void) {
    ASSERT(sweep_array_references(true));
    ASSERT(sweep_array_references(false));
    return true;
}

bool test_gc_sweep_fields_and_functions(void) {
    process_t *proc = create_process();
    size_t baseline = proc->objects.size;
    object_t *root = get_root_object();
    object_t *live = create_user_defined_object(proc, (object_array_t){&root, 1});
    object_t *key = create_string_object(proc, STATIC_STRING(U"dynamic-key"));
    object_t *character = create_character_object(proc, U'中');
    object_t *real = create_real_number_object(proc, 1234.5);
    object_t *number = create_integer_object(proc, 9876543);
    object_t *live_function = create_function_object(proc, NULL, 0, 0, live, NULL, false);
    object_t *values[] = {live_function, live, key, character, real, number};
    gc_root_handle_t handles[sizeof(values) / sizeof(*values)];
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); ++i)
        handles[i] = register_gc_root(proc, values[i]);

    object_t *dead = create_user_defined_object(proc, (object_array_t){&live, 1});
    object_t **names = ALLOC(sizeof(*names));
    names[0] = key;
    object_t *function = create_function_object(proc, names, 1, 0, dead, NULL, false);
    object_static_string_t capture_key_storage = {0};
    object_static_string_t other_key_storage = {0};
    object_t *capture_key = get_static_string_object(&capture_key_storage, U"captured");
    object_t *other = get_static_string_object(&other_key_storage, U"other");
    bool good = create_object_property(live, capture_key, get_null_object(), true) == MSTAT_OK;
    function_access_t *access = get_function_access(function);
    add_lexical_scope(access, live);
    good = add_function_field_capture(access, live, get_own_field_cell(live, capture_key)) && good;
    good = create_object_property(dead, key, function, true) == MSTAT_OK && good;
    good = create_object_property(dead, get_this_key(), dead, true) == MSTAT_OK && good;
    object_t *members[] = {character, real, number, live_function};
    object_t *array = create_array_object(proc, (object_array_t){members, 4});
    good = create_object_property(dead, other, array, true) == MSTAT_OK && good;
    DECREF(array);
    DECREF(function);
    DECREF(dead);

    collect_garbage(proc);
    collect_garbage(proc);
    good = good && proc->objects.size == baseline + sizeof(values) / sizeof(*values);
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); ++i) {
        size_t before = proc->objects.size;
        unregister_gc_root(proc, handles[i]);
        DECREF(values[i]);
        good = good && proc->objects.size + 1 == before;
    }
    good = good && proc->objects.size == baseline;
    destroy_process(proc);
    ASSERT(good);
    return true;
}

typedef struct {
    process_t *process;
    object_t *root;
    object_t *anchor;
    size_t baseline;
    size_t live;
    bool ok;
    gc_root_handle_t root_handle;
} iterative_gc_test_t;

/** @brief Runs only the collector and bounded observations on a small OS stack. */
static void *collect_on_small_stack(void *data) {
    iterative_gc_test_t *test = data;
    size_t live_bytes = get_allocated_memory_size();
    test->ok = true;
    for (size_t i = 0; i < 3; ++i) {
#ifdef GOAT_TEST_ALLOC_FAILURE
        allocation_fault_start(1);
#endif
        collect_garbage(test->process);
#ifdef GOAT_TEST_ALLOC_FAILURE
        size_t attempts = allocation_fault_stop();
        test->ok = test->ok && attempts == 0 && !allocation_fault_triggered();
#endif
        test->ok = test->ok && test->process->objects.size == test->live
                   && get_allocated_memory_size() == live_bytes;
        if (!test->ok)
            return NULL;
        object_array_t elements = get_array_elements(test->anchor);
        int_value_t marker = get_object_integer_value(elements.items[1]);
        test->ok = elements.items[0] == test->root && marker.has_value && marker.value == 456789;
    }

    /* Drop the only outside owner. The strong back-edge prevents synchronous recursive
       destruction; this test exercises marking/sweep, not the separate DECREF cascade. */
    unregister_gc_root(test->process, test->root_handle);
    DECREF(test->root);
#ifdef GOAT_TEST_ALLOC_FAILURE
    allocation_fault_start(1);
#endif
    collect_garbage(test->process);
#ifdef GOAT_TEST_ALLOC_FAILURE
    size_t attempts = allocation_fault_stop();
    test->ok = test->ok && attempts == 0 && !allocation_fault_triggered();
#endif
    test->ok = test->ok && test->process->objects.size == test->baseline;
    return NULL;
}

bool test_gc_iterative_mark(void) {
    process_t *proc = create_process();
    ASSERT(proc);
    size_t baseline = proc->objects.size;
    object_t *marker = create_integer_object(proc, 456789);
    object_t *seed[] = {get_null_object(), marker};
    object_t *anchor = create_array_object(proc, (object_array_t){seed, 2});
    DECREF(marker);

    object_t *arrays = anchor;
    INCREF(arrays);
    for (size_t i = 0; i < 24000; ++i) {
        object_t *next = create_array_object(proc, (object_array_t){&arrays, 1});
        DECREF(arrays);
        arrays = next;
    }

    object_t *root_object = get_root_object();
    object_static_string_t storage = {0};
    object_t *child_key = get_static_string_object(&storage, U"next");
    object_t *mixed = anchor;
    INCREF(mixed);
    for (size_t i = 0; i < 8000; ++i) {
        object_t *container = create_user_defined_object(proc, (object_array_t){&root_object, 1});
        ASSERT(create_object_property(container, child_key, mixed, true) == MSTAT_OK);
        ASSERT(create_object_property(container, get_this_key(), container, true) == MSTAT_OK);
        DECREF(mixed);
        object_t *function = create_function_object(proc, NULL, 0, 0, container, NULL, false);
        DECREF(container);
        mixed = create_array_object(proc, (object_array_t){&function, 1});
        DECREF(function);
    }

    enum { WIDTH = 2048 };

    object_t **branches = ALLOC(WIDTH * sizeof(*branches));
    for (size_t i = 0; i < WIDTH; ++i) {
        object_t *values[] = {anchor, anchor, marker};
        branches[i] = create_array_object(proc, (object_array_t){values, 3});
    }
    object_t *wide = create_array_object(proc, (object_array_t){branches, WIDTH});
    for (size_t i = 0; i < WIDTH; ++i)
        DECREF(branches[i]);
    FREE(branches);
    object_t *tops[] = {arrays, mixed, wide};
    object_t *root = create_array_object(proc, (object_array_t){tops, 3});
    DECREF(arrays);
    DECREF(mixed);
    DECREF(wide);
    object_t *zero = get_integer_zero();
    operation_result_t back =
        anchor->vtbl->set_index(proc, anchor, (object_array_t){&zero, 1}, root);
    ASSERT(!back.is_exception);
    DECREF(back.value);
    DECREF(anchor);
    gc_root_handle_t root_root = register_gc_root(proc, root);

    iterative_gc_test_t test = {proc, root, anchor, baseline, proc->objects.size, false, root_root};
    os_thread_t *worker;
    os_thread_status_t status =
        create_os_thread(&worker, collect_on_small_stack, &test, 256 * 1024);
    if (status != OS_THREAD_OK) {
        destroy_process(proc);
        return false;
    }
    ASSERT(join_os_thread(worker, NULL) == OS_THREAD_OK);
    bool discarded = destroy_os_thread(worker) == OS_THREAD_OK;
    if (test.ok) {
        /* A swept object from the pool must reenter the worklist with a clear link. */
        size_t pooled = proc->user_defined_objects.size;
        object_t *reused = create_user_defined_object(proc, (object_array_t){&root_object, 1});
        test.ok = pooled > 0 && proc->user_defined_objects.size == pooled - 1;
        gc_root_handle_t reused_root = register_gc_root(proc, reused);
        collect_garbage(proc);
        test.ok = test.ok && proc->objects.size == baseline + 1;
        unregister_gc_root(proc, reused_root);
        DECREF(reused);
        test.ok = test.ok && proc->objects.size == baseline;
    }
    destroy_process(proc);
    ASSERT(discarded && test.ok);
    return true;
}

/** @brief Releases the only owner of an acyclic graph from a small native stack. */
static void *release_on_small_stack(void *data) {
    iterative_gc_test_t *test = data;
#ifdef GOAT_TEST_ALLOC_FAILURE
    allocation_fault_start(1);
#endif
    clear_exception(&test->process->main_thread->exception);
#ifdef GOAT_TEST_ALLOC_FAILURE
    size_t attempts = allocation_fault_stop();
    test->ok = attempts == 0 && !allocation_fault_triggered();
#else
    test->ok = true;
#endif
    object_array_t values = get_array_elements(test->anchor);
    int_value_t marker = get_object_integer_value(values.items[0]);
    test->ok = test->ok && marker.has_value && marker.value == 456789
               && test->process->objects.size == test->baseline + 2
               && !test->process->main_thread->exception.value && !test->process->pending_releases
               && !test->process->draining_releases;
    DECREF(test->anchor);
    test->ok = test->ok && test->process->objects.size == test->baseline;
    return NULL;
}

/** @brief Tears down another process whose exception is still holding the graph. */
static void *destroy_on_small_stack(void *data) {
    destroy_process(data);
    return NULL;
}

/** @brief Builds an acyclic mixed chain with a weak self field and retained shared leaf. */
static object_t *make_release_chain(process_t *proc, object_t *anchor, size_t count) {
    object_t *next = anchor;
    INCREF(next);
    object_t *proto = get_string_builder_proto();
    static object_static_string_t storage = {0};
    object_t *key = get_static_string_object(&storage, U"next");
    for (size_t i = 0; i < count; ++i) {
        object_t *elements[] = {next, anchor, anchor};
        object_t *array = create_array_object(proc, (object_array_t){elements, 3});
        DECREF(next);
        object_t *receiver = create_user_defined_object(proc, (object_array_t){&proto, 1});
        if (!initialize_string_builder_storage(receiver)
            || create_object_property(receiver, key, array, true) != MSTAT_OK
            || create_object_property(receiver, get_this_key(), receiver, true) != MSTAT_OK) {
            DECREF(array);
            DECREF(receiver);
            return NULL;
        }
        DECREF(array);
        next = create_function_object(proc, NULL, 0, 0, receiver, NULL, false);
        DECREF(receiver);
    }
    return next;
}

typedef struct {
    process_t *process;
    object_t *extra_owner;
    size_t calls;
    bool drained_by_outer_release;
} release_callback_t;

static uint32_t GOAT_NATIVE_CALL
unused_release_test_entry(const goat_native_continuation_context_t *context, uint32_t initial_pc) {
    (void)context;
    return initial_pc;
}

/** @brief A native descriptor owner can release another object while its host is finalizing. */
static void release_callback_owner(void *data) {
    release_callback_t *callback = data;
    ++callback->calls;
    DECREF(callback->extra_owner);
    callback->drained_by_outer_release =
        callback->process->draining_releases && callback->process->pending_releases;
}

bool test_iterative_last_ref_release(void) {
    process_t *proc = create_process();
    ASSERT(proc);
    size_t baseline = proc->objects.size;
    object_t *marker = create_integer_object(proc, 456789);
    object_t *anchor = create_array_object(proc, (object_array_t){&marker, 1});
    DECREF(marker);
    object_t *mixed = make_release_chain(proc, anchor, 12000);
    ASSERT(mixed);
    /* These ordinary arrays make a second deep branch, with no cycle to defer to GC. */
    object_t *arrays = anchor;
    INCREF(arrays);
    for (size_t i = 0; i < 30000; ++i) {
        object_t *parent = create_array_object(proc, (object_array_t){&arrays, 1});
        DECREF(arrays);
        arrays = parent;
    }
    object_t *branches[] = {mixed, arrays};
    proc->main_thread->exception.value = create_array_object(proc, (object_array_t){branches, 2});
    DECREF(mixed);
    DECREF(arrays);
    iterative_gc_test_t test = {.process = proc, .anchor = anchor, .baseline = baseline};
    os_thread_t *worker;
    ASSERT(create_os_thread(&worker, release_on_small_stack, &test, 256 * 1024) == OS_THREAD_OK);
    ASSERT(join_os_thread(worker, NULL) == OS_THREAD_OK);
    ASSERT(destroy_os_thread(worker) == OS_THREAD_OK);
    ASSERT(test.ok);

    size_t pooled = proc->user_defined_objects.size;
    object_t *proto = get_string_builder_proto();
    object_t *reused = create_user_defined_object(proc, (object_array_t){&proto, 1});
    ASSERT(pooled > 0 && proc->user_defined_objects.size == pooled - 1);
    ASSERT(!get_string_builder_storage(reused));
    gc_root_handle_t reused_root = register_gc_root(proc, reused);
    collect_garbage(proc);
    ASSERT(proc->objects.size == baseline + 1);
    unregister_gc_root(proc, reused_root);
    DECREF(reused);

    object_t *array = create_array_object(proc, (object_array_t){NULL, 0});
    release_callback_t callback = {.process = proc, .extra_owner = array};
    const goat_native_continuation_entry_t entry = {.function_id = 1,
                                                    .instruction_count = 1,
                                                    .invoke = unused_release_test_entry};
    native_function_descriptor_t *descriptor =
        create_native_continuation_descriptor(&entry, &callback, release_callback_owner);
    ASSERT(descriptor);
    object_t *function =
        create_function_object(proc, NULL, 0, 0, get_root_object(), descriptor, false);
    release_native_function_descriptor(descriptor);
    DECREF(function);
    ASSERT(callback.calls == 1 && callback.drained_by_outer_release);
    ASSERT(proc->objects.size == baseline && !proc->pending_releases && !proc->draining_releases);

    proc->main_thread->exception.value = make_release_chain(proc, get_null_object(), 8000);
    ASSERT(proc->main_thread->exception.value);
    ASSERT(create_os_thread(&worker, destroy_on_small_stack, proc, 256 * 1024) == OS_THREAD_OK);
    ASSERT(join_os_thread(worker, NULL) == OS_THREAD_OK);
    ASSERT(destroy_os_thread(worker) == OS_THREAD_OK);
    return true;
}

typedef struct {
    process_t *process;
    size_t releases;
    bool registration_closed;
} root_teardown_probe_t;

/** @brief Observes ordinary disposal of a registered function's native descriptor. */
static void count_root_resource_release(void *data) {
    root_teardown_probe_t *probe = data;
    ++probe->releases;
    probe->registration_closed = !register_gc_root(probe->process, get_null_object()).serial;
}

bool test_owning_gc_roots(void) {
    size_t allocated_before = get_allocated_memory_size();
    process_t *proc = create_process();
    process_t *other = create_process();
    ASSERT(proc && other);
    size_t baseline = proc->objects.size;
    object_t *marker = create_integer_object(proc, 456789);
    object_t *obj = create_array_object(proc, (object_array_t){&marker, 1});
    DECREF(marker);
    gc_root_handle_t first = register_gc_root(proc, obj);
    gc_root_handle_t second = register_gc_root(proc, obj);
    gc_root_handle_t same_index_other = register_gc_root(other, get_null_object());
    ASSERT(first.serial && second.serial && same_index_other.serial);
    ASSERT(!register_gc_root(proc, NULL).serial);
    ASSERT(!register_gc_root(NULL, obj).serial);
    ASSERT(!register_gc_root(other, obj).serial);
    ASSERT(!unregister_gc_root(other, first));
    ASSERT(!unregister_gc_root(NULL, first));
    ASSERT(unregister_gc_root(other, same_index_other));
    destroy_process(other);

    /* The two registrations alone own the array and its child. */
    DECREF(obj);
    ASSERT(unregister_gc_root(proc, first));
    ASSERT(!unregister_gc_root(proc, first));
    collect_garbage(proc);
    ASSERT(proc->objects.size == baseline + 2);
    int_value_t value = get_object_integer_value(get_array_elements(obj).items[0]);
    ASSERT(value.has_value && value.value == 456789);

    /* Reuse freed slots, but old copied handles still cannot remove a new registration. */
    enum { MANY = 2048 };

    gc_root_handle_t *more = ALLOC(MANY * sizeof(*more));
#ifdef GOAT_TEST_ALLOC_FAILURE
    allocation_fault_start(SIZE_MAX);
#endif
    size_t initial_capacity = proc->gc_roots_capacity;
    size_t growths = 0;
    for (size_t i = 0; i < MANY; ++i) {
        more[i] = register_gc_root(proc, obj);
        ASSERT(more[i].serial);
        if (initial_capacity != proc->gc_roots_capacity) {
            ++growths;
            initial_capacity = proc->gc_roots_capacity;
        }
    }
#ifdef GOAT_TEST_ALLOC_FAILURE
    size_t attempts = allocation_fault_stop();
    ASSERT(attempts == growths && !allocation_fault_triggered());
    allocation_fault_start(1);
#endif
    ASSERT(growths > 0 && growths < 16);
    ASSERT(!unregister_gc_root(proc, first) && proc->gc_roots_size == MANY + 1);
    for (size_t i = 0; i < MANY; ++i)
        ASSERT(unregister_gc_root(proc, more[i]));
    ASSERT(unregister_gc_root(proc, second));
#ifdef GOAT_TEST_ALLOC_FAILURE
    size_t removals = allocation_fault_stop();
    ASSERT(!removals && !allocation_fault_triggered());
#endif
    FREE(more);
    ASSERT(!proc->gc_roots_size && proc->objects.size == baseline);

    /* Leave two roots registered at shutdown with a shared child and native resources.
       The function also retains its closure through the lexical-access snapshot. */
    object_t *proto = get_string_builder_proto();
    object_t *child = create_user_defined_object(proc, (object_array_t){&proto, 1});
    string_builder_t *buffer = initialize_string_builder_storage(child);
    ASSERT(buffer);
    append_substring(buffer, U"root-only", 9);
    root_teardown_probe_t probe = {.process = proc};
    const goat_native_continuation_entry_t entry = {.function_id = 1,
                                                    .instruction_count = 1,
                                                    .invoke = unused_release_test_entry};
    native_function_descriptor_t *descriptor =
        create_native_continuation_descriptor(&entry, &probe, count_root_resource_release);
    ASSERT(descriptor);
    object_t *fn = create_function_object(proc, NULL, 0, 0, child, descriptor, false);
    release_native_function_descriptor(descriptor);
    object_t *siblings[] = {child, child};
    object_t *array = create_array_object(proc, (object_array_t){siblings, 2});
    DECREF(child);
    ASSERT(register_gc_root(proc, array).serial && register_gc_root(proc, fn).serial);
    DECREF(array);
    DECREF(fn);
    collect_garbage(proc);
    ASSERT(!probe.releases && proc->objects.size == baseline + 3);
    destroy_process(proc);
    ASSERT(probe.releases == 1 && probe.registration_closed
           && get_allocated_memory_size() == allocated_before);
    return true;
}

bool test_checked_gc_refcounts(void) {
    /* Exercise boundary arithmetic on detached counters, not fabricated live objects. */
    gc_refcount_t count = 0;
    ASSERT(!try_retain_gc_ref(&count) && count == 0);
    ASSERT(try_release_gc_ref(&count) == GC_REFCOUNT_INVALID && count == 0);
    ASSERT(!drop_swept_gc_ref(&count) && count == 0);

    count = UINT32_MAX - 1;
    ASSERT(try_retain_gc_ref(&count) && count == UINT32_MAX);
    ASSERT(!try_retain_gc_ref(&count) && count == UINT32_MAX);
    ASSERT(try_release_gc_ref(&count) == GC_REFCOUNT_LIVE && count == UINT32_MAX - 1);

    count = 1;
    ASSERT(!drop_swept_gc_ref(&count) && count == 1);
    retain_gc_ref(&count);
    ASSERT(count == 2 && drop_swept_gc_ref(&count) && count == 1);
    ASSERT(release_gc_ref(&count) && count == 0);

    /* Normal pooled shells become active again before they can be retained. */
    process_t *proc = create_process();
    ASSERT(proc);
    size_t live = proc->objects.size;
    for (size_t i = 0; i < 32; ++i) {
        object_t *obj = create_integer_object(proc, 456789);
        gc_root_handle_t root = register_gc_root(proc, obj);
        ASSERT(root.serial && proc->objects.size == live + 1);
        DECREF(obj);
        collect_garbage(proc);
        ASSERT(get_object_integer_value(obj).value == 456789);
        ASSERT(unregister_gc_root(proc, root));
        ASSERT(proc->objects.size == live && proc->integers.size == 1);
    }
    destroy_process(proc);
    return true;
}

enum { LARGE_OBJECTS = 24, LARGE_FIELDS = 1024, LARGE_PROTOTYPES = 96 };

/** @brief Populates all four user-object vectors with more than a warm shell should keep. */
static bool fill_large_pool_workload(process_t *proc, object_t **objects) {
    object_t *prototype = get_root_object();
    object_t *parents[LARGE_PROTOTYPES];
    for (size_t i = 0; i < LARGE_PROTOTYPES; ++i)
        parents[i] = create_user_defined_object(proc, (object_array_t){&prototype, 1});
    for (size_t i = 0; i < LARGE_OBJECTS; ++i) {
        objects[i] = create_user_defined_object(proc, (object_array_t){parents, LARGE_PROTOTYPES});
        for (size_t j = 0; j < LARGE_FIELDS; ++j) {
            object_t *key = create_integer_object(proc, 3000 + (int64_t)j);
            model_status_t status =
                create_object_property(objects[i], key, get_null_object(), true);
            DECREF(key);
            if (status != MSTAT_OK)
                return false;
        }
    }
    for (size_t i = 0; i < LARGE_PROTOTYPES; ++i)
        DECREF(parents[i]);
    return true;
}

bool test_gc_pool_budget(void) {
    size_t begin = get_allocated_memory_size();
    process_t *proc = create_process();
    ASSERT(proc);
    object_t *live = create_integer_object(proc, 734567);
    object_t *objects[LARGE_OBJECTS];
    ASSERT(fill_large_pool_workload(proc, objects));
    size_t occupied = get_allocated_memory_size();
    for (size_t i = 0; i < LARGE_OBJECTS; ++i)
        DECREF(objects[i]);
    ASSERT(occupied - get_allocated_memory_size() > 1024 * 1024);
    ASSERT(proc->user_defined_objects.size > 0 && proc->pooled_bytes <= GOAT_POOL_BYTE_LIMIT);
    object_t *reused_shell = proc->user_defined_objects.head;
    size_t previously_pooled = proc->pooled_bytes;
    object_t *root = get_root_object();
    object_t *reused = create_user_defined_object(proc, (object_array_t){&root, 1});
    ASSERT(reused == reused_shell && get_object_keys(reused).size == 0
           && get_object_prototypes(reused).size == 1 && proc->pooled_bytes < previously_pooled);
    DECREF(reused);

    /* A full real pool stays bounded even when the integer pool is empty. */
    trim_process_object_pools(proc);

    enum { EXTRA = 17 };

    object_t *reals[GOAT_POOL_TYPE_LIMIT + EXTRA];
    for (size_t i = 0; i < GOAT_POOL_TYPE_LIMIT + EXTRA; ++i)
        reals[i] = create_real_number_object(proc, 500.25 + (double)i);
    for (size_t i = 0; i < GOAT_POOL_TYPE_LIMIT + EXTRA; ++i)
        DECREF(reals[i]);
    ASSERT(!proc->integers.size && proc->real_numbers.size == GOAT_POOL_TYPE_LIMIT);

    /* Include each scalar type in exact trim accounting. */
    object_t *integer = create_integer_object(proc, 734568);
    object_t *character = create_character_object(proc, U'界');
    object_t *string = create_string_object(proc, STATIC_STRING(U"pool-budget"));
    DECREF(integer);
    DECREF(character);
    DECREF(string);
    ASSERT(proc->integers.size && proc->characters.size && proc->dynamic_strings.size);

    /* Saturate the byte limit with retained warm vectors across different types. */
    object_t **holders = ALLOC(GOAT_POOL_TYPE_LIMIT * sizeof(*holders));
    for (size_t i = 0; i < GOAT_POOL_TYPE_LIMIT; ++i) {
        holders[i] = create_user_defined_object(proc, (object_array_t){&root, 1});
        for (size_t j = 0; j < GOAT_POOL_VECTOR_SLOT_LIMIT; ++j) {
            object_t *key = get_static_integer_object((int)j);
            ASSERT(create_object_property(holders[i], key, get_null_object(), true) == MSTAT_OK);
        }
    }
    for (size_t i = 0; i < GOAT_POOL_TYPE_LIMIT; ++i)
        DECREF(holders[i]);
    FREE(holders);
    ASSERT(proc->user_defined_objects.size > 0
           && proc->user_defined_objects.size < GOAT_POOL_TYPE_LIMIT);
    ASSERT(proc->pooled_bytes <= GOAT_POOL_BYTE_LIMIT && proc->pooled_bytes > 400 * 1024);

    size_t before_trim = get_allocated_memory_size();
    size_t expected = proc->pooled_bytes;
#ifdef GOAT_TEST_ALLOC_FAILURE
    allocation_fault_start(1);
#endif
    size_t released = trim_process_object_pools(proc);
#ifdef GOAT_TEST_ALLOC_FAILURE
    size_t attempts = allocation_fault_stop();
    ASSERT(!attempts && !allocation_fault_triggered());
#endif
    ASSERT(released == expected && before_trim - get_allocated_memory_size() == released);
    ASSERT(!proc->pooled_bytes && !proc->user_defined_objects.size && !proc->real_numbers.size
           && !proc->integers.size && !proc->characters.size && !proc->dynamic_strings.size);
    ASSERT(get_object_integer_value(live).value == 734567 && trim_process_object_pools(proc) == 0);
    DECREF(live);
    destroy_process(proc);
    ASSERT(get_allocated_memory_size() == begin);
    return true;
}

bool test_gc_pool_benchmark(void) {
    enum { ITERATIONS = 100000 };

    size_t begin = get_allocated_memory_size();
    process_t *proc = create_process();
    ASSERT(proc);
    size_t before = get_allocated_memory_size();
    object_t *large[LARGE_OBJECTS];
    ASSERT(fill_large_pool_workload(proc, large));
    size_t peak = get_allocated_memory_size() - before;
    for (size_t i = 0; i < LARGE_OBJECTS; ++i)
        DECREF(large[i]);
    size_t retained = get_allocated_memory_size() - before;
    size_t trimmed = trim_process_object_pools(proc);

    /* Use a fresh process for the small benchmark on both runtimes. */
    destroy_process(proc);
    proc = create_process();
    ASSERT(proc);
    object_t *root = get_root_object();
    object_t *key = get_static_integer_object(0);
    for (size_t warm = 0; warm < 32; ++warm) {
        object_t *object = create_user_defined_object(proc, (object_array_t){&root, 1});
        ASSERT(create_object_property(object, key, get_null_object(), true) == MSTAT_OK);
        DECREF(object);
    }
#ifdef GOAT_TEST_ALLOC_FAILURE
    allocation_fault_start(SIZE_MAX);
#endif
    clock_t started = clock();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        object_t *object = create_user_defined_object(proc, (object_array_t){&root, 1});
        object_t *value = create_integer_object(proc, 734567);
        ASSERT(create_object_property(object, key, value, true) == MSTAT_OK);
        DECREF(value);
        DECREF(object);
    }
    double elapsed = (double)(clock() - started) / CLOCKS_PER_SEC;
#ifdef GOAT_TEST_ALLOC_FAILURE
    size_t allocations = allocation_fault_stop();
    ASSERT(!allocation_fault_triggered());
#else
    size_t allocations = 0; /* Counter disabled in builds without allocation instrumentation. */
#endif
    printf("large_peak=%zu large_retained=%zu trimmed=%zu small_iterations=%d "
           "small_allocations=%zu small_cpu_seconds=%.6f\n",
           peak,
           retained,
           trimmed,
           ITERATIONS,
           allocations,
           elapsed);
    destroy_process(proc);
    ASSERT(get_allocated_memory_size() == begin);
    return true;
}

/** @brief Sets one array slot using normal ownership-transfer semantics. */
static bool set_stat_test_edge(process_t *proc, object_t *array, size_t index, object_t *target) {
    object_t *key = get_static_integer_object((int)index);
    operation_result_t result =
        array->vtbl->set_index(proc, array, (object_array_t){&key, 1}, target);
    DECREF(result.value);
    return !result.is_exception;
}

bool test_gc_statistics(void) {
    size_t start = get_allocated_memory_size();
    process_t *a = create_process();
    process_t *b = create_process();
    ASSERT(a && b);
    gc_stats_t sa, sb;
    ASSERT(configure_gc_statistics(a, &sa) && configure_gc_statistics(b, &sb));
    collect_garbage(a);
    size_t base_objects = sa.last.after.objects;
    size_t base_edges = sa.last.edges;
    size_t base_payload = sa.last.after.active_bytes;

    object_t *leaf = create_real_number_object(a, 314.25);
    object_t *live = create_array_object(a, (object_array_t){&leaf, 1});
    DECREF(leaf);
    gc_root_handle_t live_root = register_gc_root(a, live);
    object_t *empty = get_null_object();
    object_t *first = create_array_object(a, (object_array_t){&empty, 1});
    object_t *second = create_array_object(a, (object_array_t){&empty, 1});
    ASSERT(set_stat_test_edge(a, first, 0, second) && set_stat_test_edge(a, second, 0, first));
    DECREF(first);
    DECREF(second);

    object_t *b_live = create_string_object(b, STATIC_STRING(U"other process"));
    gc_root_handle_t b_root = register_gc_root(b, b_live);
    object_t *b_temp = create_integer_object(b, 456789);
    DECREF(b_temp);
    size_t b_active = b->objects.size, b_pooled = b->pooled_bytes;

    size_t global_before = get_allocated_memory_size();
#ifdef GOAT_TEST_ALLOC_FAILURE
    allocation_fault_start(1);
#endif
    collect_garbage(a);
#ifdef GOAT_TEST_ALLOC_FAILURE
    size_t attempts = allocation_fault_stop();
    ASSERT(!attempts && !allocation_fault_triggered());
#endif
    ASSERT(sa.collections == 2 && sa.explicit_collections == 2 && !sa.run_end_collections);
    ASSERT(sa.last.before.objects == base_objects + 4 && sa.last.after.objects == base_objects + 2);
    ASSERT(sa.last.vertices == base_objects + 2 && sa.last.edges == base_edges + 2);
    ASSERT(sa.last.after.active_bytes > base_payload && sa.last.before.auxiliary_bytes > 0);
    ASSERT(sa.last.before.auxiliary_bytes == sa.last.after.auxiliary_bytes);
    ASSERT(sa.last.freed_bytes == global_before - get_allocated_memory_size());
    ASSERT(!sb.collections && !sb.trim_calls && b->objects.size == b_active
           && b->pooled_bytes == b_pooled);
    ASSERT(b_live->vtbl->type == TYPE_STRING && b->gc_roots_size == 1);
    string_value_t other_text = convert_object_to_string(b_live);
    ASSERT(STRCMP(other_text.data, U"other process") == 0);
    FREE_STRING(other_text);

    object_t *temp = create_integer_object(a, 7654321);
    DECREF(temp);
    size_t pooled = a->pooled_bytes;
    ASSERT(pooled > 0 && trim_process_object_pools(a) == pooled);
    ASSERT(sa.trim_calls == 1 && sa.trimmed_bytes == pooled && sa.collections == 2
           && sa.last.edges == base_edges + 2);
    ASSERT(!sb.collections && !sb.trim_calls && b->pooled_bytes == b_pooled);

    ASSERT(unregister_gc_root(a, live_root) && unregister_gc_root(b, b_root));
    DECREF(live);
    DECREF(b_live);
    instruction_t instructions[] = {{.opcode = END}};
    bytecode_t *code = create_test_bytecode(instructions, 1);
    ASSERT(run(a, code) == 0);
    ASSERT(sa.collections == 3 && sa.run_end_collections == 1 && sa.last.run_end);
    free_bytecode(code);
    ASSERT(configure_gc_statistics(a, NULL));
    collect_garbage(a);
    ASSERT(sa.collections == 3);
    destroy_process(a);
    destroy_process(b);
    ASSERT(get_allocated_memory_size() == start);
    return true;
}

bool test_gc_statistics_benchmark(void) {
    enum { DEPTH = 20000, WIDTH = 4096, TRIALS = 9, CHURN = 100000 };

    size_t memory_before = get_allocated_memory_size();
    process_t *proc = create_process();
    ASSERT(proc);
    gc_stats_t stats;
    object_t *empty = get_null_object();
    object_t *seed = create_array_object(proc, (object_array_t){&empty, 1});
    object_t *top = seed;
    INCREF(top);
    for (size_t i = 0; i < DEPTH; ++i) {
        object_t *next = create_array_object(proc, (object_array_t){&top, 1});
        DECREF(top);
        top = next;
    }
    ASSERT(set_stat_test_edge(proc, seed, 0, top));
    object_t **values = ALLOC(WIDTH * sizeof(*values));
    for (size_t i = 0; i < WIDTH; ++i)
        values[i] = top;
    object_t *wide = create_array_object(proc, (object_array_t){values, WIDTH});
    FREE(values);
    gc_root_handle_t root = register_gc_root(proc, wide);
    DECREF(top);
    DECREF(seed);
    uint64_t maximum_unobserved = 0;
    size_t unobserved_samples = 0;
    for (size_t trial = 0; trial < TRIALS; ++trial) {
        uint64_t started, finished;
        bool timed = gc_statistics_now(&started);
        collect_garbage(proc);
        if (timed && gc_statistics_now(&finished) && finished >= started) {
            ++unobserved_samples;
            if (finished - started > maximum_unobserved)
                maximum_unobserved = finished - started;
        }
    }
    printf("unobserved_samples=%zu unobserved_max_pause_ns=%llu\n",
           unobserved_samples,
           (unsigned long long)maximum_unobserved);
    ASSERT(configure_gc_statistics(proc, &stats));
    for (size_t trial = 0; trial < TRIALS; ++trial)
        collect_garbage(proc);
    printf("live_passes=%llu vertices=%zu edges=%zu active_bytes=%zu max_pause_ns=%llu timed=%d\n",
           (unsigned long long)stats.collections,
           stats.last.vertices,
           stats.last.edges,
           stats.last.after.active_bytes,
           (unsigned long long)stats.max_pause_ns,
           stats.last.clock_valid);
    ASSERT(unregister_gc_root(proc, root));
    DECREF(wide);
    collect_garbage(proc);
    printf("cycle_before=%zu cycle_after=%zu cycle_freed_bytes=%zu cycle_pause_ns=%llu\n",
           stats.last.before.objects,
           stats.last.after.objects,
           stats.last.freed_bytes,
           (unsigned long long)stats.last.pause_ns);

    uint64_t start = 0, end = 0;
    bool timed = gc_statistics_now(&start);
    for (size_t i = 0; i < CHURN; ++i) {
        object_t *scalar = create_integer_object(proc, 97654321);
        object_t *array = create_array_object(proc, (object_array_t){&scalar, 1});
        DECREF(scalar);
        DECREF(array);
    }
    timed = timed && gc_statistics_now(&end) && end >= start;
    size_t trimmed = trim_process_object_pools(proc);
    printf("rc_churn=%d rc_churn_ns=%llu rc_timed=%d pool_trimmed_bytes=%zu "
           "final_tracing_passes=%llu\n",
           CHURN,
           (unsigned long long)(timed ? end - start : 0),
           timed,
           trimmed,
           (unsigned long long)stats.collections);
    destroy_process(proc);
    ASSERT(get_allocated_memory_size() == memory_before);
    return true;
}

typedef struct {
    process_t *process;
    unsigned stage;
    bool rooted, inspected;
    bool result_preserved, depth_preserved;
} controlled_wait_t;

/** @brief A model-test mutator: publishes before waiting and does not mutate until resumed. */
static void *hold_published_result(void *data) {
    controlled_wait_t *state = data;
    process_t *proc = state->process;
    process_execution_enter(proc);
    process_execution_enter(proc);
    object_t *leaf = create_integer_object(proc, 789123);
    operation_result_t pending =
        operation_success(create_array_object(proc, (object_array_t){&leaf, 1}));
    DECREF(leaf);
    gc_root_handle_t published = register_gc_root(proc, pending.value);
    /* The registration, not a C local, now owns the graph across the wait. */
    DECREF(pending.value);
    state->rooted = published.serial != 0;
    state->stage = 1;
    broadcast_os_condition(proc->threads_changed);
    while (state->stage == 1)
        if (wait_os_condition(proc->threads_changed, proc->execution_mutex) != OS_THREAD_OK)
            abort();
    state->depth_preserved = os_mutex_depth(proc->execution_mutex) == 2;
    if (state->inspected) {
        object_array_t items = get_array_elements(pending.value);
        state->result_preserved =
            items.size == 1 && get_object_integer_value(items.items[0]).value == 789123;
        state->result_preserved = unregister_gc_root(proc, published) && state->result_preserved;
    }
    process_execution_leave(proc);
    process_execution_leave(proc);
    return NULL;
}

bool test_gc_controlled_model_boundary(void) {
    size_t baseline = get_allocated_memory_size();
    process_t *proc = create_process();
    ASSERT(proc);
    gc_stats_t stats;
    ASSERT(configure_gc_statistics(proc, &stats));
    controlled_wait_t test = {.process = proc};
    os_thread_t *worker;
    process_execution_enter(proc);
    if (create_os_thread(&worker, hold_published_result, &test, 0) != OS_THREAD_OK) {
        process_execution_leave(proc);
        destroy_process(proc);
        return false;
    }
    while (!test.stage)
        if (wait_os_condition(proc->threads_changed, proc->execution_mutex) != OS_THREAD_OK)
            abort();
    /* Both participants explicitly agree the only mutator is parked with its result rooted.
       This is a test-host protocol, not a permission granted by any VM unlock. */
    if (test.rooted && os_mutex_depth(proc->execution_mutex) == 1 && proc->gc_roots_size == 1) {
        size_t active = proc->objects.size;
        collect_garbage(proc);
        collect_garbage(proc);
        test.inspected = stats.collections == 2 && stats.explicit_collections == 2
                         && stats.last.after.objects == active;
    }
    test.stage = 2;
    broadcast_os_condition(proc->threads_changed);
    process_execution_leave(proc);
    ASSERT(join_os_thread(worker, NULL) == OS_THREAD_OK);
    ASSERT(destroy_os_thread(worker) == OS_THREAD_OK);
    /* Do not dereference a result unless the invariant was observed. */
    if (!test.inspected)
        return false;
    bool good = test.depth_preserved && test.result_preserved && !proc->gc_roots_size;
    destroy_process(proc);
    ASSERT(good && get_allocated_memory_size() == baseline);
    return true;
}

/** @brief An ordinary builtin is still not the outer post-opcode dispatcher. */
static operation_result_t
check_unproven_gc_boundary(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    process_t *process = thread->process;
    bool accepted = gc_admission_allows(thread, (gc_admission_boundary_t){.opcode = CALL});
    bool correct = !count && !accepted && process->execution && process->active_threads == 1
                   && os_mutex_depth(process->execution_mutex) == 1
                   && process->gc_statistics->collections == 0;
    return operation_success(get_boolean_object(correct));
}

bool test_gc_admission_decision(void) {
    const gc_admission_boundary_t candidate = {.origin = GC_DISPATCH_STANDALONE_OUTER,
                                               .opcode = ARRAY,
                                               .dispatch_continues = true,
                                               .ordinary_flow = true,
                                               .transfers_published = true,
                                               .no_worker_started = true,
                                               .no_other_clients = true};
    ASSERT(gc_admission_matches_boundary(candidate));
    ASSERT(!gc_admission_matches_boundary((gc_admission_boundary_t){0}));
    gc_admission_boundary_t refused = candidate;
    refused.origin = GC_DISPATCH_UNPROVEN;
    ASSERT(!gc_admission_matches_boundary(refused));
    refused = candidate;
    refused.opcode = CALL;
    ASSERT(!gc_admission_matches_boundary(refused));
    refused = candidate;
    refused.dispatch_continues = false;
    ASSERT(!gc_admission_matches_boundary(refused));
    refused = candidate;
    refused.ordinary_flow = false;
    ASSERT(!gc_admission_matches_boundary(refused));
    refused = candidate;
    refused.transfers_published = false;
    ASSERT(!gc_admission_matches_boundary(refused));
    refused = candidate;
    refused.no_worker_started = false;
    ASSERT(!gc_admission_matches_boundary(refused));
    refused = candidate;
    refused.no_other_clients = false;
    ASSERT(!gc_admission_matches_boundary(refused));

    process_t *process = create_process();
    ASSERT(process);
    thread_t *thread = process->main_thread;
    ASSERT(!gc_admission_allows(thread, candidate));
    process_execution_enter(process);
    gc_stats_t stats;
    bool observed = configure_gc_statistics(process, &stats);
    bool idle_refused =
        !gc_admission_allows(thread, candidate) && os_mutex_depth(process->execution_mutex) == 1;
    process_execution_leave(process);

    static const builtin_function_t descriptor = {.name = U"checkGcBoundary",
                                                  .execute = check_unproven_gc_boundary};
    static object_static_string_t key_storage;
    static builtin_function_object_t function_storage;
    object_t *key = get_static_string_object(&key_storage, descriptor.name);
    object_t *function = get_builtin_function_object(&function_storage, &descriptor);
    model_status_t installed = create_object_property(thread->context->data, key, function, true);

    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    uint32_t name = add_string_to_data_segment(data, descriptor.name);
    add_instruction(builder, (instruction_t){.opcode = VLOAD, .arg1 = name});
    add_instruction(builder, (instruction_t){.opcode = CALL});
    add_instruction(builder, (instruction_t){.opcode = END});
    bytecode_t *code = link_code_and_data(builder, data);
    destroy_code_builder(builder);
    destroy_data_builder(data);

    bool normal = observed && installed == MSTAT_OK && run(process, code) == 0
                  && thread->data_stack->size == 1
                  && peek_object_from_stack(thread->data_stack, 0) == get_boolean_object(true)
                  && stats.collections == 1 && stats.run_end_collections == 1
                  && stats.explicit_collections == 0;
    destroy_process(process);
    free_bytecode(code);
    ASSERT(idle_refused && normal);
    return true;
}
