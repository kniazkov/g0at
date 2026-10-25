/**
 * @file test_model.c
 * @copyright 2026 Ivan Kniazkov
 * @brief A set of tests for testing object model.
 */

#include "test_model.h"

#include "codegen/linker.h"
#include "lib/allocate.h"
#include "lib/split64.h"
#include "model/context.h"
#include "model/object.h"
#include "model/process.h"
#include "model/thread.h"
#include "test_macro.h"
#include "vm/vm.h"

#include <stdio.h>

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
    ASSERT(topology.size = 2);
    const gchar_t *expected = U"{\"length\"=0;\"trim\"=trim;\"charAt\"=charAt;"
                              U"\"substring\"=substring;\"contains\"=contains;"
                              U"\"indexOf\"=indexOf;\"lastIndexOf\"=lastIndexOf;"
                              U"\"startsWith\"=startsWith;\"endsWith\"=endsWith;"
                              U"\"isEmpty\"=isEmpty;\"isBlank\"=isBlank;"
                              U"\"toUpperCase\"=toUpperCase;\"toLowerCase\"=toLowerCase;"
                              U"\"replace\"=replace;\"repeat\"=repeat;\"format\"=format}";
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
        {U"Character", get_character_proto},
        {U"Function", get_function_proto},
        {U"Integer", get_integer_proto},
        {U"Null", get_null_object},
        {U"Number", get_numeric_proto},
        {U"Object", get_root_object},
        {U"Real", get_real_proto},
        {U"String", get_string_proto},
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
