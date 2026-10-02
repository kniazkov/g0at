/** @file test_native_conversion.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Integer boundaries, fallback ownership, abstract domains and input lines.
 */
#include "test_native_conversion.h"

#include "analysis/function_call.h"
#include "analysis/lattice.h"
#include "builtins/registry.h"
#include "lib/allocate.h"
#include "lib/io.h"
#include "model/process.h"
#include "model/thread.h"
#include "test_macro.h"

#include <math.h>
#include <stdio.h>

bool test_native_int() {
    struct {
        const wchar_t *text;
        double real;
        bool is_real, valid;
        int64_t expected;
    } cases[] = {{L"0", 0, false, true, 0},
                 {L" -0012 \t", 0, false, true, -12},
                 {L"+42", 0, false, true, 42},
                 {L"9223372036854775807", 0, false, true, INT64_MAX},
                 {L"-9223372036854775808", 0, false, true, INT64_MIN},
                 {L"9223372036854775808", 0, false, false, 0},
                 {L"-9223372036854775809", 0, false, false, 0},
                 {L"99999999999999999999999999999999", 0, false, false, 0},
                 {L"", 0, false, false, 0},
                 {L"  ", 0, false, false, 0},
                 {L"+", 0, false, false, 0},
                 {L"--1", 0, false, false, 0},
                 {L"1.5", 0, false, false, 0},
                 {L"1e2", 0, false, false, 0},
                 {L"0xff", 0, false, false, 0},
                 {L"12x", 0, false, false, 0},
                 {L"nan", 0, false, false, 0},
                 {L"inf", 0, false, false, 0},
                 {L"\u0661", 0, false, false, 0},
                 {NULL, 3.9, true, true, 3},
                 {NULL, -3.9, true, true, -3},
                 {NULL, -0.0, true, true, 0},
                 {NULL, -0x1p63, true, true, INT64_MIN},
                 {NULL, 0x1p63, true, false, 0},
                 {NULL, NAN, true, false, 0},
                 {NULL, INFINITY, true, false, 0},
                 {NULL, -INFINITY, true, false, 0}};

    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        for (size_t count = 1; count <= 2; count++) {
            process_t *proc = create_process();
            arena_t *arena = create_arena(8);
            abstract_state_t *state = create_abstract_state(arena);
            object_t *arg;
            const lattice_element_t *abstract;
            if (cases[i].is_real) {
                arg = create_real_number_object(proc, cases[i].real);
                abstract = make_real_constant_element(arena, cases[i].real);
            } else {
                string_value_t text = {cases[i].text, wcslen(cases[i].text), false};
                arg = create_string_object(proc, text);
                abstract = make_string_constant_element(arena, VALUE_TO_VIEW(text));
            }
            object_t *fallback = create_string_object(proc, STATIC_STRING(L"fallback"));
            if (count == 2)
                push_object_onto_stack(proc->main_thread->data_stack, fallback);
            else
                DECREF(fallback);
            push_object_onto_stack(proc->main_thread->data_stack, arg);
            ASSERT(call_object(get_function_int(), count, proc->main_thread));
            object_t *result = pop_object_from_stack(proc->main_thread->data_stack);
            const lattice_element_t *args[] = {
                abstract,
                make_string_constant_element(arena, (string_view_t){L"fallback", 8})};
            const lattice_element_t *value =
                interpret_function_call(make_builtin_function_element(arena, &builtin_int),
                                        args,
                                        count,
                                        state);
            if (cases[i].valid || count == 1) {
                int64_t expected = cases[i].valid ? cases[i].expected : 0;
                ASSERT(is_integer_object(result)
                       && get_object_integer_value(result).value == expected);
                ASSERT(value->type == LATTICE_INTEGER_CONSTANT);
                ASSERT(((const integer_constant_element_t *)value)->value == expected);
            } else {
                ASSERT(result == fallback && value->type == LATTICE_STRING_CONSTANT);
                string_value_t text = convert_object_to_string(result);
                ASSERT(!wcscmp(text.data, L"fallback"));
                FREE_STRING(text);
            }
            DECREF(result);
            destroy_process(proc);
            destroy_abstract_state(state);
            destroy_arena(arena);
        }
    }
    return true;
}

bool test_native_int_domains() {
    arena_t *arena = create_arena(8);
    abstract_state_t *state = create_abstract_state(arena);
    const lattice_element_t *args[] = {make_real_element(), make_null_element()};
    const lattice_element_t *f = make_builtin_function_element(arena, &builtin_int);
    ASSERT(interpret_function_call(f, args, 1, state)->type == LATTICE_INTEGER);
    ASSERT(interpret_function_call(f, args, 2, state)->type == LATTICE_TOP);
    args[0] = make_integer_range_element(arena, -4, 9);
    ASSERT(interpret_function_call(f, args, 2, state) == args[0]);
    args[0] = make_boolean_element();
    const lattice_element_t *value = interpret_function_call(f, args, 2, state);
    ASSERT(value->type == LATTICE_INTEGER_RANGE);
    ASSERT(((const integer_range_element_t *)value)->min == 0);
    ASSERT(((const integer_range_element_t *)value)->max == 1);
    args[0] = make_function_element();
    ASSERT(interpret_function_call(f, args, 2, state) == args[1]);
    ASSERT(interpret_function_call(make_builtin_function_element(arena, &builtin_input),
                                   NULL,
                                   0,
                                   state)
               ->type
           == LATTICE_STRING);
    ASSERT(!state->builtin_bindings_unknown && state->control_flow == FLOW_NORMAL);
    args[0] = make_string_element();
    args[1] = make_string_element();
    ASSERT(interpret_function_call(f, args, 2, state)->type == LATTICE_NOT_NULL);
    size_t count;
    const builtin_function_t *const *functions = get_builtin_functions(&count);
    for (size_t i = 0; i < count; i++) {
        const builtin_function_t *math = functions[i];
        if (math == &builtin_int || math == &builtin_input || math == &builtin_print
            || math == &builtin_sign)
            continue;
        args[0] = make_numeric_element();
        args[1] = make_top_element();
        ASSERT(math->interpret(state, args, math->min_args)->type == LATTICE_REAL);
        for (size_t bad = 0; bad < math->min_args; bad++) {
            args[0] = args[1] = make_real_element();
            args[bad] = make_string_element();
            ASSERT(math->interpret(state, args, math->min_args)->type == LATTICE_BOTTOM);
        }
    }
    destroy_abstract_state(state);
    destroy_arena(arena);
    return true;
}

bool test_input_lines() {
    const char *path = "test-native-input.tmp";
    FILE *file = fopen(path, "w+b");
    ASSERT(file);
    const char content[] =
        "\r\n\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xF0\x9F\x98\x80\n";
    ASSERT(fwrite(content, 1, sizeof(content) - 1, file) == sizeof(content) - 1);
    for (size_t i = 0; i < 10000; i++)
        fputc('x', file);
    rewind(file);
    string_value_t text = read_input_line(file);
    ASSERT(text.data && text.length == 0);
    FREE_STRING(text);
    text = read_input_line(file);
    ASSERT(text.data && !wcscmp(text.data, L"\u041f\u0440\u0438\u0432\u0435\u0442 \U0001f600"));
    FREE_STRING(text);
    text = read_input_line(file);
    ASSERT(text.data && text.length == 10000 && text.data[9999] == L'x');
    FREE_STRING(text);
    text = read_input_line(file);
    ASSERT(text.data && text.length == 0);
    FREE_STRING(text);
    ASSERT(!fclose(file));
    file = fopen(path, "w+b");
    ASSERT(file);
    fputc(0xff, file);
    fputc('\n', file);
    fputc(0, file);
    rewind(file);
    text = read_input_line(file);
    ASSERT(!text.data);
    text = read_input_line(file);
    ASSERT(!text.data);
    ASSERT(!fclose(file) && !remove(path));
    return true;
}
