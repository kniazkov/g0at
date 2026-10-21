/** @file test_unary.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Unary model, VM, AST and parser contracts.
 */
#include "test_unary.h"

#include "analysis/unary_operation.h"
#include "analysis_test_support.h"
#include "codegen/linker.h"
#include "codegen/source_builder.h"
#include "graph/binary_operation.h"
#include "graph/unary_expression.h"
#include "lib/allocate.h"
#include "model/process.h"
#include "model/thread.h"
#include "test_macro.h"
#include "vm/gc.h"
#include "vm/vm.h"

#include <math.h>
#include <stdio.h>

static bool same_real(double a, double b) {
    return (isnan(a) && isnan(b)) || (a == b && (a != 0 || !!signbit(a) == !!signbit(b)));
}

static bytecode_t *make_code(const instruction_t *list, size_t count) {
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    for (size_t i = 0; i < count; i++)
        add_instruction(builder, list[i]);
    bytecode_t *code = link_code_and_data(builder, data);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    return code;
}

bool test_unary_models_and_vm(void) {
    int64_t ints[] = {0, 1, -1, 9000, -9000, INT64_MAX, INT64_MIN};
    int64_t negated[] = {0, -1, 1, -9000, 9000, -INT64_MAX, INT64_MAX};
    double reals[] = {0.0, -0.0, 0.5, -1.5, INFINITY, -INFINITY, NAN};
    for (int minus = 0; minus < 2; minus++) {
        for (int real = 0; real < 2; real++) {
            for (size_t i = 0; i < 7; i++) {
                process_t *proc = create_process();
                arena_t *arena = create_arena(4);
                object_t *operand = real ? create_real_number_object(proc, reals[i])
                                         : create_integer_object(proc, ints[i]);
                operation_result_t result =
                    minus ? unary_minus_object(proc, operand) : unary_plus_object(proc, operand);
                ASSERT(!result.is_exception && result.value);
                if (!minus)
                    ASSERT(result.value == operand);
                const lattice_element_t *value =
                    real ? make_real_constant_element(arena, reals[i])
                         : make_integer_constant_element(arena, ints[i]);
                value = lattice_unary(arena, value, minus);
                if (real) {
                    double expected = minus ? -reals[i] : reals[i];
                    ASSERT(!is_integer_object(result.value));
                    ASSERT(same_real(get_object_real_value(result.value).value, expected));
                    ASSERT(value->type == LATTICE_REAL_CONSTANT);
                    ASSERT(same_real(((const real_constant_element_t *)value)->value, expected));
                } else {
                    ASSERT(is_integer_object(result.value));
                    ASSERT(get_object_integer_value(result.value).value
                           == (minus ? negated[i] : ints[i]));
                    ASSERT(value->type == LATTICE_INTEGER_CONSTANT);
                    ASSERT(((const integer_constant_element_t *)value)->value
                           == (minus ? negated[i] : ints[i]));
                }
                DECREF(result.value);
                push_object_onto_stack(proc->main_thread->data_stack, operand);
                instruction_t list[] = {{.opcode = minus ? UMINUS : UPLUS}, {.opcode = END}};
                bytecode_t *code = make_code(list, 2);
                string_value_t text = bytecode_to_text(code);
                ASSERT(wcsstr(text.data, minus ? L"UMINUS" : L"UPLUS"));
                FREE_STRING(text);
                ASSERT(run(proc, code) == 0);
                /* An unretained alias would be recycled here. */
                object_t *other = real ? create_real_number_object(proc, 99.5)
                                       : create_integer_object(proc, 99000);
                DECREF(other);
                collect_garbage(proc);
                object_t *actual = peek_object_from_stack(proc->main_thread->data_stack, 0);
                ASSERT(proc->main_thread->data_stack->size == 1);
                if (real) {
                    ASSERT(same_real(get_object_real_value(actual).value,
                                     minus ? -reals[i] : reals[i]));
                } else {
                    ASSERT(get_object_integer_value(actual).value
                           == (minus ? negated[i] : ints[i]));
                }
                free_bytecode(code);
                destroy_process(proc);
                destroy_arena(arena);
            }
        }
    }
    return true;
}

bool test_unary_errors(void) {
    object_t *invalid[] = {get_null_object(),
                           get_boolean_object(true),
                           get_empty_string(),
                           get_function_print(),
                           get_exceptions_object(),
                           get_integer_proto(),
                           get_real_proto()};
    process_t *proc = create_process();
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        operation_result_t plus = unary_plus_object(proc, invalid[i]);
        operation_result_t minus = unary_minus_object(proc, invalid[i]);
        ASSERT(plus.is_exception && plus.value == get_exception_invalid_operation());
        ASSERT(minus.is_exception && minus.value == plus.value);
        DECREF(plus.value);
        DECREF(minus.value);
    }
    destroy_process(proc);
    for (int minus = 0; minus < 2; minus++) {
        for (int caught = 0; caught < 2; caught++) {
            instruction_t list[] = {{.opcode = ILOAD32, .arg1 = 7},
                                    {.opcode = TRY, .arg1 = 6},
                                    {.opcode = ILOAD32, .arg1 = 9},
                                    {.opcode = NIL},
                                    {.opcode = minus ? UMINUS : UPLUS},
                                    {.opcode = END},
                                    {.opcode = END}};
            if (!caught)
                list[1].opcode = NOP;
            bytecode_t *code = make_code(list, 7);
            proc = create_process();
            ASSERT((run(proc, code) == 0) == !!caught);
            ASSERT(proc->main_thread->data_stack->size == (caught ? 2 : 0));
            ASSERT((caught ? peek_object_from_stack(proc->main_thread->data_stack, 0)
                           : proc->main_thread->exception.value)
                   == get_exception_invalid_operation());
            destroy_process(proc);
            free_bytecode(code);
        }
    }
    return true;
}

bool test_unary_ast_and_domains(void) {
    arena_t *arena = create_arena(8);
    for (int minus = 0; minus < 2; minus++) {
        expression_t *operand = create_addition_node(arena,
                                                     (expression_t *)create_integer_node(arena, 2),
                                                     (expression_t *)create_integer_node(arena, 3));
        expression_t *node = minus ? create_unary_minus_node(arena, operand)
                                   : create_unary_plus_node(arena, operand);
        ASSERT(is_expression(node->base.vtbl->type));
        ASSERT(get_node_child_count(&node->base) == 1);
        ASSERT(get_node_child(&node->base, 0) == &operand->base);
        ASSERT(!get_node_child(&node->base, 1) && !get_node_child(&node->base, SIZE_MAX));
        ASSERT(!wcscmp(get_node_child_tag(&node->base, 0), L"operand"));
        ASSERT(!get_node_child_tag(&node->base, 1));
        string_value_t source = generate_goat_code_from_expression(node);
        ASSERT(!wcscmp(source.data, minus ? L"-2 + 3" : L"+2 + 3"));
        source_builder_t *builder = create_source_builder();
        generate_indented_goat_code_from_expression(node, builder, 0);
        string_value_t indented = build_source(builder);
        ASSERT(indented.length == source.length + 1
               && !wmemcmp(source.data, indented.data, source.length)
               && indented.data[source.length] == L'\n');
        FREE_STRING(source);
        FREE_STRING(indented);
        destroy_source_builder(builder);
        abstract_state_t *state = create_abstract_state(arena);
        const lattice_element_t *value = calculate_expression(node, state, arena);
        ASSERT(value->type == LATTICE_INTEGER_CONSTANT);
        ASSERT(((const integer_constant_element_t *)value)->value == (minus ? -5 : 5));
        destroy_abstract_state(state);
        code_builder_t *code = create_code_builder();
        data_builder_t *data = create_data_builder();
        generate_bytecode_from_expression(node, code, data);
        add_instruction(code, (instruction_t){.opcode = END});
        bytecode_t *linked = link_code_and_data(code, data);
        process_t *proc = create_process();
        ASSERT(run(proc, linked) == 0);
        ASSERT(
            get_object_integer_value(peek_object_from_stack(proc->main_thread->data_stack, 0)).value
            == (minus ? -5 : 5));
        destroy_process(proc);
        free_bytecode(linked);
        destroy_code_builder(code);
        destroy_data_builder(data);
    }
    const lattice_element_t *range = make_integer_range_element(arena, -2, 5);
    ASSERT(lattice_unary(arena, range, false) == range);
    const integer_range_element_t *neg =
        (const integer_range_element_t *)lattice_unary(arena, range, true);
    ASSERT(neg->base.type == LATTICE_INTEGER_RANGE && neg->min == -5 && neg->max == 2);
    ASSERT(lattice_unary(arena, make_integer_range_element(arena, INT64_MIN, -1), true)->type
           == LATTICE_INTEGER_RANGE);
    ASSERT(lattice_unary(arena, make_null_element(), false)->type == LATTICE_BOTTOM);
    ASSERT(lattice_unary(arena, make_bottom_element(), true)->type == LATTICE_BOTTOM);
    ASSERT(lattice_unary(arena, make_real_element(), true)->type == LATTICE_REAL);
    ASSERT(lattice_unary(arena, make_numeric_element(), true)->type == LATTICE_NUMERIC);
    ASSERT(lattice_unary(arena, make_top_element(), true)->type == LATTICE_TOP);
    destroy_arena(arena);
    return true;
}

bool test_unary_parser(void) {
    const wchar_t *valid[] = {L"-2**2",
                              L"2**-3",
                              L"-2**-3**-2",
                              L"+ + +1",
                              L"1- -2",
                              L"1+-2",
                              L"2*-3",
                              L"var x=-2; +x",
                              L"-(1+2)",
                              L"var f=func { return -1; }; -f()",
                              L"throw -1",
                              L"-9223372036854775808",
                              L"-2**3**2"};
    const wchar_t *invalid[] =
        {L"+", L"-", L"1+-", L"-*2", L"2**-", L"-( )", L"var x=+;", L"print(-)", L"-2**", L"-2**+"};
    for (size_t i = 0; i < sizeof(valid) / sizeof(*valid); i++) {
        arena_t *arena = create_arena(8);
        parser_memory_t memory = {arena, arena, arena, arena};
        node_t *root =
            parse_analysis_test_program(&memory,
                                        (string_value_t){valid[i], wcslen(valid[i]), false});
        ASSERT(root);
        string_value_t source = generate_goat_code_from_node(root);
        ASSERT(parse_analysis_test_program(&memory, source));
        FREE_STRING(source);
        destroy_arena(arena);
    }
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        arena_t *arena = create_arena(8);
        parser_memory_t memory = {arena, arena, arena, arena};
        ASSERT(
            !parse_analysis_test_program(&memory,
                                         (string_value_t){invalid[i], wcslen(invalid[i]), false}));
        destroy_arena(arena);
    }
    return true;
}
