/**
 * @file test_character.c
 * @copyright 2026 Ivan Kniazkov
 * @brief A set of tests for the character type.
 */

#include "test_character.h"

#include "analysis/function_call.h"
#include "analysis/lattice.h"
#include "builtins/registry.h"
#include "codegen/linker.h"
#include "graph/node.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "model/object.h"
#include "model/process.h"
#include "model/thread.h"
#include "resources/messages.h"
#include "scanner/scanner.h"
#include "test_macro.h"
#include "vm/vm.h"

#include <stdio.h>
#include <string.h>

bool test_character_scanner() {
    arena_t *arena = create_arena(8);
    parser_memory_t memory = {arena, arena, arena, arena};
    token_groups_t groups;
    scanner_t *scan = create_scanner("program.goat",
                                     STATIC_STRING(L"'a' '\\n' '\\'' '' 'ab' '\\q'"),
                                     &memory,
                                     &groups);
    const wchar_t *notations[] = {L"'a'", L"'\\n'", L"'\\''"};
    for (size_t i = 0; i < 3; i++) {
        token_t *tok = get_token(scan);
        ASSERT(tok && tok->type == TOKEN_EXPRESSION && tok->node != NULL);
        ASSERT(tok->node->vtbl->type == NODE_CHARACTER);
        string_value_t code = generate_goat_code_from_node(tok->node);
        ASSERT(!wcscmp(code.data, notations[i]));
        FREE_STRING(code);
    }
    token_t *tok = get_token(scan);
    ASSERT(tok && tok->type == TOKEN_ERROR);
    ASSERT(!wcscmp(tok->text.data, get_messages()->empty_character_literal));
    tok = get_token(scan);
    ASSERT(tok && tok->type == TOKEN_ERROR);
    ASSERT(!wcscmp(tok->text.data, get_messages()->invalid_character_literal));
    tok = get_token(scan);
    ASSERT(tok && tok->type == TOKEN_ERROR);
    string_view_t formatted =
        format_string_to_arena(arena, get_messages()->invalid_escape_sequence, L'q');
    ASSERT(!wcscmp(tok->text.data, formatted.data));
    const wchar_t *valid[] = {L"'\\0'", L"'\\U0001F600'", L"'\\Uffffffff'"};
    const uint32_t codes[] = {0, 0x1F600, UINT32_MAX};
    for (size_t i = 0; i < 3; i++) {
        scan = create_scanner("program.goat",
                              (string_value_t){valid[i], wcslen(valid[i]), false},
                              &memory,
                              &groups);
        tok = get_token(scan);
        ASSERT(tok && tok->type == TOKEN_EXPRESSION && tok->node);
        const lattice_element_t *value = calculate_node(tok->node, NULL, NULL);
        ASSERT(value->type == LATTICE_CHAR_CONSTANT);
        ASSERT(((const char_constant_element_t *)value)->value == codes[i]);
    }
    const wchar_t *invalid[] = {L"'\\U123'", L"'\\U0000000g'", L"'\\U000000001'", L"'\\U"};
    for (size_t i = 0; i < 4; i++) {
        scan = create_scanner("program.goat",
                              (string_value_t){invalid[i], wcslen(invalid[i]), false},
                              &memory,
                              &groups);
        tok = get_token(scan);
        ASSERT(tok && tok->type == TOKEN_ERROR);
    }
    destroy_arena(arena);
    return true;
}

bool test_character_object() {
    process_t *proc = create_process();
    object_t *a = create_character_object(proc, L'a');
    ASSERT(is_character_object(a));
    ASSERT(a->vtbl->type == TYPE_CHARACTER);
    string_value_t text = convert_object_to_string(a);
    ASSERT(!wcscmp(text.data, L"a"));
    FREE_STRING(text);
    text = convert_object_to_string_notation(a);
    ASSERT(!wcscmp(text.data, L"'a'"));
    FREE_STRING(text);
    int_value_t integer = get_object_integer_value(a);
    ASSERT(integer.has_value && integer.value == L'a');
    ASSERT(get_object_boolean_value(a));

    object_t *newline = create_character_object(proc, L'\n');
    text = convert_object_to_string_notation(newline);
    ASSERT(!wcscmp(text.data, L"'\\n'"));
    FREE_STRING(text);

    object_t *b = create_character_object(proc, L'b');
    operation_result_t result = are_objects_equal(proc, a, a);
    ASSERT(!result.is_exception && get_object_boolean_value(result.value));
    result = are_objects_equal(proc, a, b);
    ASSERT(!result.is_exception && !get_object_boolean_value(result.value));
    result = are_objects_not_equal(proc, a, b);
    ASSERT(!result.is_exception && get_object_boolean_value(result.value));
    result = is_object_less_than(proc, a, b);
    ASSERT(!result.is_exception && get_object_boolean_value(result.value));
    result = is_object_greater_than(proc, b, a);
    ASSERT(!result.is_exception && get_object_boolean_value(result.value));
    result = is_object_less_or_equal(proc, a, a);
    ASSERT(!result.is_exception && get_object_boolean_value(result.value));

    result = add_objects(proc, a, b);
    ASSERT(!result.is_exception);
    ASSERT(result.value->vtbl->type == TYPE_STRING);
    text = convert_object_to_string(result.value);
    ASSERT(!wcscmp(text.data, L"ab"));
    FREE_STRING(text);

    object_t *str = create_string_object(proc, STATIC_STRING(L"c"));
    result = add_objects(proc, a, str);
    ASSERT(!result.is_exception);
    text = convert_object_to_string(result.value);
    ASSERT(!wcscmp(text.data, L"ac"));
    FREE_STRING(text);
    destroy_process(proc);
    return true;
}

bool test_character_vm() {
    instruction_t list[] = {{.opcode = CLOAD, .arg1 = L'a'},
                            {.opcode = CLOAD, .arg1 = L'z'},
                            {.opcode = END}};
    code_builder_t *code_builder = create_code_builder();
    data_builder_t *data_builder = create_data_builder();
    for (size_t i = 0; i < 3; i++)
        add_instruction(code_builder, list[i]);
    bytecode_t *code = link_code_and_data(code_builder, data_builder);
    destroy_code_builder(code_builder);
    destroy_data_builder(data_builder);
    process_t *proc = create_process();
    run(proc, code);
    ASSERT(proc->main_thread->data_stack->size == 2);
    object_t *first = peek_object_from_stack(proc->main_thread->data_stack, 1);
    object_t *second = peek_object_from_stack(proc->main_thread->data_stack, 0);
    ASSERT(is_character_object(first) && is_character_object(second));
    ASSERT(get_object_integer_value(first).value == L'a');
    ASSERT(get_object_integer_value(second).value == L'z');
    destroy_process(proc);
    free_bytecode(code);
    return true;
}

bool test_ord_chr_builtin() {
    process_t *proc = create_process();
    arena_t *arena = create_arena(8);
    abstract_state_t *state = create_abstract_state(arena);

    push_object_onto_stack(proc->main_thread->data_stack, create_character_object(proc, L'a'));
    ASSERT(call_object(get_function_ord(), 1, proc->main_thread));
    object_t *result = pop_object_from_stack(proc->main_thread->data_stack);
    ASSERT(is_integer_object(result) && get_object_integer_value(result).value == L'a');
    DECREF(result);
    const lattice_element_t *args[] = {make_char_constant_element(arena, L'a')};
    const lattice_element_t *value =
        interpret_function_call(make_builtin_function_element(arena, &builtin_ord), args, 1, state);
    ASSERT(value->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(((const integer_constant_element_t *)value)->value == L'a');

    push_object_onto_stack(proc->main_thread->data_stack, create_integer_object(proc, 122));
    ASSERT(call_object(get_function_chr(), 1, proc->main_thread));
    result = pop_object_from_stack(proc->main_thread->data_stack);
    ASSERT(is_character_object(result) && get_object_integer_value(result).value == L'z');
    DECREF(result);
    args[0] = make_integer_constant_element(arena, 122);
    value =
        interpret_function_call(make_builtin_function_element(arena, &builtin_chr), args, 1, state);
    ASSERT(value->type == LATTICE_CHAR_CONSTANT);
    ASSERT(((const char_constant_element_t *)value)->value == L'z');

    args[0] = make_char_element();
    value =
        interpret_function_call(make_builtin_function_element(arena, &builtin_ord), args, 1, state);
    ASSERT(value->type == LATTICE_INTEGER);
    args[0] = make_integer_element();
    value =
        interpret_function_call(make_builtin_function_element(arena, &builtin_chr), args, 1, state);
    ASSERT(value->type == LATTICE_CHAR);

    push_object_onto_stack(proc->main_thread->data_stack, create_integer_object(proc, 7));
    ASSERT(!call_object(get_function_ord(), 1, proc->main_thread));
    ASSERT(proc->main_thread->exception.value == get_exception_invalid_argument());
    push_object_onto_stack(proc->main_thread->data_stack,
                           create_string_object(proc, STATIC_STRING(L"a")));
    ASSERT(!call_object(get_function_chr(), 1, proc->main_thread));
    ASSERT(proc->main_thread->exception.value == get_exception_invalid_argument());

    destroy_abstract_state(state);
    destroy_arena(arena);
    destroy_process(proc);
    return true;
}
