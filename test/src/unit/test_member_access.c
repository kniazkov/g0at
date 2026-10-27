/**
 * @file test_member_access.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Member access (the `.` operator) tests across scanner, parser, AST and VM.
 */

#include "test_member_access.h"

#include "analysis_test_support.h"
#include "codegen/linker.h"
#include "graph/member_access.h"
#include "graph/node.h"
#include "graph/variable.h"
#include "lib/memory/allocate.h"
#include "lib/memory/arena.h"
#include "model/object.h"
#include "model/process.h"
#include "model/thread.h"
#include "parser/parser.h"
#include "scanner/scanner.h"
#include "test_macro.h"
#include "vm/gc.h"
#include "vm/vm.h"

#include <stdio.h>
#include <string.h>

bool test_member_access_scanner(void) {
    arena_t *arena = create_arena(8);
    parser_memory_t memory = {arena, arena, arena, arena};
    token_groups_t *groups = (token_groups_t *)CALLOC(sizeof(token_groups_t));
    scanner_t *scan = create_scanner("program.goat", STATIC_STRING(U"obj . x"), &memory, groups);
    token_t *token = get_token(scan);
    ASSERT(token && token->type == TOKEN_IDENTIFIER);
    token = get_token(scan);
    ASSERT(token && token->type == TOKEN_OPERATOR);
    ASSERT(!STRCMP(token->text.data, U"."));
    token = get_token(scan);
    ASSERT(token && token->type == TOKEN_IDENTIFIER);
    ASSERT(groups->member_access_operators.count == 1);
    ASSERT(groups->member_access_operators.first == groups->member_access_operators.last);

    scan = create_scanner("program.goat", STATIC_STRING(U"a.b.c"), &memory, groups);
    token_list_t tokens;
    compilation_error_t *error = process_brackets(&memory, scan, &tokens, groups);
    ASSERT(error == NULL);
    ASSERT(groups->member_access_operators.count == 2);
    parsing_result_t parsing_result = {0};
    error = apply_reduction_rules(groups, &memory, &parsing_result);
    ASSERT(error == NULL);
    ASSERT(tokens.count == 1);
    ASSERT(tokens.first->type == TOKEN_EXPRESSION);
    ASSERT(tokens.first->node->vtbl->type == NODE_MEMBER_ACCESS);
    string_value_t code = generate_goat_code_from_node(tokens.first->node);
    ASSERT(!STRCMP(code.data, U"a.b.c"));
    FREE_STRING(code);
    FREE(groups);
    destroy_arena(arena);
    return true;
}

bool test_member_access_parser(void) {
    const gchar_t *valid[] = {
        U"var obj = {}; obj.x = 10; print(obj.x);",
        U"var obj = {}; obj.a.b = 20;",
        U"var obj = {}; (obj).x = 5;",
        U"var obj = {}; print(obj.x);",
        U"var obj = {}; obj.x;",
        U"obj.x = obj.y;",
        U"var obj = {const f = func() { return 1; }}; obj.f();",
        U"var obj = {const f = func(x) { return x; }}; obj.f(42);",
        U"var obj = {var inner = {}; inner.g = func() { return 2; }}; obj.inner.g();",
        U"var obj = {const f = func() { return 1; }}; (obj).f();",
        U"var obj = {}; obj.f = func() { return 3; }; print(obj.f());"};
    for (size_t i = 0; i < sizeof(valid) / sizeof(*valid); i++) {
        arena_t *a = create_arena(8);
        parser_memory_t m = {a, a, a, a};
        ASSERT(
            parse_analysis_test_program(&m, (string_value_t){valid[i], STRLEN(valid[i]), false}));
        destroy_arena(a);
    }
    const gchar_t *invalid[] = {U"obj.;",
                                U".x;",
                                U"var obj = {}; obj..x;",
                                U"var obj = {}; obj.1;",
                                U"f(1,);",
                                U"f(,1);",
                                U"var obj = {}; obj.f(1,);"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        arena_t *a = create_arena(8);
        parser_memory_t m = {a, a, a, a};
        ASSERT(
            !parse_analysis_test_program(&m,
                                         (string_value_t){invalid[i], STRLEN(invalid[i]), false}));
        destroy_arena(a);
    }
    return true;
}

bool test_member_access_ast(void) {
    arena_t *arena = create_arena(8);
    variable_t *var = (variable_t *)create_variable_node(arena, (string_view_t){U"obj", 3});
    expression_t *node =
        create_member_access_node(arena, &var->base.base, (string_view_t){U"x", 1});
    ASSERT(is_expression(node->base.vtbl->type));
    ASSERT(node->base.vtbl->is_assignable_expression);
    ASSERT(get_node_child_count(&node->base) == 1);
    ASSERT(get_node_child(&node->base, 0) == &var->base.base.base);
    ASSERT(!get_node_child(&node->base, 1));

    string_value_t source = generate_goat_code_from_expression(node);
    ASSERT(!STRCMP(source.data, U"obj.x"));
    FREE_STRING(source);

    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    generate_bytecode_from_expression(node, builder, data);
    ASSERT(builder->size == 2);
    ASSERT(builder->instructions[0].opcode == VLOAD);
    ASSERT(builder->instructions[1].opcode == GETPROP);
    ASSERT(builder->instructions[1].arg1 == 1);
    ASSERT(data->descriptors_count == 2);
    destroy_code_builder(builder);
    destroy_data_builder(data);

    builder = create_code_builder();
    data = create_data_builder();
    generate_bytecode_assign_from_expression(node, builder, data);
    ASSERT(builder->size == 2);
    ASSERT(builder->instructions[0].opcode == VLOAD);
    ASSERT(builder->instructions[1].opcode == SETPROP);
    ASSERT(builder->instructions[1].arg1 == 1);
    ASSERT(data->descriptors_count == 2);
    destroy_code_builder(builder);
    destroy_data_builder(data);

    source_builder_t *sb = create_source_builder();
    generate_indented_goat_code_from_expression(node, sb, 0);
    string_value_t pretty = build_source(sb);
    string_value_t indented_source = generate_goat_code_from_expression(node);
    ASSERT(pretty.length == indented_source.length + 1
           && !MEMCMP(pretty.data, indented_source.data, indented_source.length));
    FREE_STRING(pretty);
    FREE_STRING(indented_source);
    destroy_source_builder(sb);
    destroy_arena(arena);
    return true;
}

bool test_member_access_method_call_ast(void) {
    arena_t *arena = create_arena(8);
    variable_t *var = (variable_t *)create_variable_node(arena, (string_view_t){U"obj", 3});
    expression_t *member =
        create_member_access_node(arena, &var->base.base, (string_view_t){U"foo", 3});
    node_t *call = create_function_call_node_without_args(arena, member);
    ASSERT(call->vtbl->type == NODE_FUNCTION_CALL);
    ASSERT(get_node_child_count(call) == 1);
    ASSERT(get_node_child(call, 0) == &member->base);
    ASSERT(get_node_child(call, 0)->vtbl->type == NODE_MEMBER_ACCESS);

    string_value_t source = generate_goat_code_from_node(call);
    ASSERT(!STRCMP(source.data, U"obj.foo()"));
    FREE_STRING(source);

    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    generate_bytecode_from_node(call, builder, data);
    ASSERT(builder->size == 4);
    ASSERT(builder->instructions[0].opcode == VLOAD);
    ASSERT(builder->instructions[1].opcode == DUP);
    ASSERT(builder->instructions[2].opcode == GETPROP);
    ASSERT(builder->instructions[3].opcode == CALLM);
    ASSERT(data->descriptors_count == 2);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    destroy_arena(arena);
    return true;
}

static bytecode_t *make_member_access_code(const instruction_t *list, size_t count) {
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    add_string_to_data_segment(data, U"x");
    add_string_to_data_segment(data, U"y");
    for (size_t i = 0; i < count; i++)
        add_instruction(builder, list[i]);
    bytecode_t *code = link_code_and_data(builder, data);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    return code;
}

bool test_member_access_vm(void) {
    process_t *proc = create_process();
    object_t *root = get_root_object();
    object_t *key = create_string_object(proc, (string_value_t){U"x", 1, false});
    add_gc_root(proc, key);
    object_t *obj = create_user_defined_object(proc, (object_array_t){&root, 1});
    add_gc_root(proc, obj);
    ASSERT(create_object_property(obj, key, get_static_integer_object(10), false) == MSTAT_OK);

    instruction_t getprop[] = {{.opcode = GETPROP, .arg1 = 0}, {.opcode = END}};
    bytecode_t *code = make_member_access_code(getprop, 2);
    INCREF(obj);
    push_object_onto_stack(proc->main_thread->data_stack, obj);
    proc->main_thread->instr_id = 0;
    ASSERT(run(proc, code) == 0);
    ASSERT(proc->main_thread->data_stack->size == 1);
    object_t *result = peek_object_from_stack(proc->main_thread->data_stack, 0);
    ASSERT(is_integer_object(result));
    ASSERT(get_object_integer_value(result).value == 10);
    free_bytecode(code);

    instruction_t missing[] = {{.opcode = GETPROP, .arg1 = 1}, {.opcode = END}};
    code = make_member_access_code(missing, 2);
    INCREF(obj);
    push_object_onto_stack(proc->main_thread->data_stack, obj);
    proc->main_thread->instr_id = 0;
    ASSERT(run(proc, code) == 0);
    ASSERT(peek_object_from_stack(proc->main_thread->data_stack, 0) == get_null_object());
    free_bytecode(code);

    object_t *temp = create_user_defined_object(proc, (object_array_t){&root, 1});
    object_t *temp_value = create_integer_object(proc, 1000);
    ASSERT(create_object_property(temp, key, temp_value, false) == MSTAT_OK);
    code = make_member_access_code(getprop, 2);
    INCREF(temp);
    push_object_onto_stack(proc->main_thread->data_stack, temp);
    proc->main_thread->instr_id = 0;
    ASSERT(run(proc, code) == 0);
    ASSERT(get_object_integer_value(peek_object_from_stack(proc->main_thread->data_stack, 0)).value
           == 1000);
    free_bytecode(code);

    object_t *fresh = create_user_defined_object(proc, (object_array_t){&root, 1});
    add_gc_root(proc, fresh);
    instruction_t setprop[] = {{.opcode = SETPROP, .arg1 = 0}, {.opcode = END}};
    code = make_member_access_code(setprop, 2);
    push_object_onto_stack(proc->main_thread->data_stack, get_static_integer_object(42));
    INCREF(fresh);
    push_object_onto_stack(proc->main_thread->data_stack, fresh);
    proc->main_thread->instr_id = 0;
    ASSERT(run(proc, code) == 0);
    ASSERT(get_object_integer_value(get_object_property(fresh, key)).value == 42);
    free_bytecode(code);

    object_t *proto = create_user_defined_object(proc, (object_array_t){&root, 1});
    add_gc_root(proc, proto);
    ASSERT(create_object_property(proto, key, get_static_integer_object(7), false) == MSTAT_OK);
    object_t *derived = create_user_defined_object(proc, (object_array_t){&proto, 1});
    add_gc_root(proc, derived);
    instruction_t inherited[] = {{.opcode = GETPROP, .arg1 = 0}, {.opcode = END}};
    code = make_member_access_code(inherited, 2);
    INCREF(derived);
    push_object_onto_stack(proc->main_thread->data_stack, derived);
    proc->main_thread->instr_id = 0;
    ASSERT(run(proc, code) == 0);
    ASSERT(get_object_integer_value(peek_object_from_stack(proc->main_thread->data_stack, 0)).value
           == 7);
    free_bytecode(code);

    instruction_t setprop2[] = {{.opcode = SETPROP, .arg1 = 0}, {.opcode = END}};
    code = make_member_access_code(setprop2, 2);
    push_object_onto_stack(proc->main_thread->data_stack, get_static_integer_object(9));
    INCREF(derived);
    push_object_onto_stack(proc->main_thread->data_stack, derived);
    proc->main_thread->instr_id = 0;
    ASSERT(run(proc, code) == 0);
    ASSERT(get_object_integer_value(get_object_property(proto, key)).value == 9);
    free_bytecode(code);

    object_t *locked = create_user_defined_object(proc, (object_array_t){&root, 1});
    add_gc_root(proc, locked);
    ASSERT(create_object_property(locked, key, get_null_object(), true) == MSTAT_OK);
    instruction_t const_prop[] = {{.opcode = SETPROP, .arg1 = 0}, {.opcode = END}};
    code = make_member_access_code(const_prop, 2);
    push_object_onto_stack(proc->main_thread->data_stack, get_static_integer_object(1));
    INCREF(locked);
    push_object_onto_stack(proc->main_thread->data_stack, locked);
    proc->main_thread->instr_id = 0;
    ASSERT(run(proc, code) != 0);
    ASSERT(proc->main_thread->exception.value == get_exception_property_is_constant());
    free_bytecode(code);

    code = make_member_access_code(getprop, 2);
    string_value_t text = bytecode_to_text(code);
    ASSERT(STRSTR(text.data, U"GETPROP"));
    FREE_STRING(text);
    free_bytecode(code);

    remove_gc_root(proc, obj);
    DECREF(obj);
    remove_gc_root(proc, fresh);
    DECREF(fresh);
    remove_gc_root(proc, proto);
    DECREF(proto);
    remove_gc_root(proc, derived);
    DECREF(derived);
    remove_gc_root(proc, locked);
    DECREF(locked);
    remove_gc_root(proc, key);
    DECREF(key);
    destroy_process(proc);
    return true;
}

/** @brief Tests that CALLM passes the receiver as the method context and 'this' resolves to it. */
bool test_method_call_receiver_vm(void) {
    data_builder_t *data_builder = create_data_builder();
    uint32_t name_x_idx = add_string_to_data_segment(data_builder, U"x");
    uint32_t name_this_idx = add_string_to_data_segment(data_builder, U"this");
    code_builder_t *code_builder = create_code_builder();
    add_instruction(code_builder, (instruction_t){.opcode = ARG, .arg1 = 4});
    add_instruction(code_builder, (instruction_t){.opcode = FUNC, .arg0 = 0, .arg1 = 0});
    add_instruction(code_builder, (instruction_t){.opcode = CALLM, .arg0 = 0});
    add_instruction(code_builder, (instruction_t){.opcode = END});
    add_instruction(code_builder, (instruction_t){.opcode = VLOAD, .arg1 = name_this_idx});
    add_instruction(code_builder, (instruction_t){.opcode = GETPROP, .arg1 = name_x_idx});
    add_instruction(code_builder, (instruction_t){.opcode = RET});
    bytecode_t *code = link_code_and_data(code_builder, data_builder);
    destroy_code_builder(code_builder);
    destroy_data_builder(data_builder);

    process_t *proc = create_process();
    object_t *root = get_root_object();
    object_t *receiver = create_user_defined_object(proc, (object_array_t){&root, 1});
    add_gc_root(proc, receiver);
    object_t *key = create_string_object(proc, (string_value_t){U"x", 1, false});
    add_gc_root(proc, key);
    ASSERT(create_object_property(receiver, key, get_static_integer_object(5), false) == MSTAT_OK);
    INCREF(receiver);
    push_object_onto_stack(proc->main_thread->data_stack, receiver);
    proc->main_thread->instr_id = 0;
    ASSERT(run(proc, code) == 0);
    ASSERT(proc->main_thread->data_stack->size == 1);
    object_t *result = peek_object_from_stack(proc->main_thread->data_stack, 0);
    ASSERT(is_integer_object(result));
    ASSERT(get_object_integer_value(result).value == 5);
    free_bytecode(code);
    remove_gc_root(proc, receiver);
    DECREF(receiver);
    remove_gc_root(proc, key);
    DECREF(key);
    destroy_process(proc);
    return true;
}
