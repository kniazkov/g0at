/** @file test_update.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Update operators across model, VM, graph and parser.
 */
#include "test_update.h"

#include "analysis/update.h"
#include "analysis_test_support.h"
#include "codegen/linker.h"
#include "codegen/source_builder.h"
#include "graph/update_expression.h"
#include "graph/variable.h"
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

bool test_update_models_vm(void) {
    int64_t ints[] = {0, 1, -1, 9000, -9000, INT64_MAX, INT64_MIN};
    int64_t expected_int[2][7] = {{1, 2, 0, 9001, -8999, INT64_MAX, INT64_MIN + 1},
                                  {-1, 0, -2, 8999, -9001, INT64_MAX - 1, INT64_MIN}};
    double reals[] = {0.0, -0.0, 0.5, -1.5, INFINITY, -INFINITY, NAN, 0x1p53};
    double expected_real[2][8] = {
        {1, 1, 1.5, -0.5, INFINITY, -INFINITY, NAN, 0x1p53},
        {-1, -1, -0.5, -2.5, INFINITY, -INFINITY, NAN, 9007199254740991.0}};
    for (int decrement = 0; decrement < 2; decrement++) {
        for (int real = 0; real < 2; real++) {
            for (size_t i = 0; i < (real ? 8 : 7); i++) {
                process_t *proc = create_process();
                arena_t *arena = create_arena(8);
                object_t *operand = real ? create_real_number_object(proc, reals[i])
                                         : create_integer_object(proc, ints[i]);
                operation_result_t r =
                    decrement ? decrement_object(proc, operand) : increment_object(proc, operand);
                ASSERT(!r.is_exception && is_integer_object(r.value) == !real);
                const lattice_element_t *v =
                    lattice_update(arena,
                                   real ? make_real_constant_element(arena, reals[i])
                                        : make_integer_constant_element(arena, ints[i]),
                                   decrement);
                if (real) {
                    ASSERT(same_real(get_object_real_value(r.value).value,
                                     expected_real[decrement][i]));
                    ASSERT(v->type == LATTICE_REAL_CONSTANT);
                    ASSERT(same_real(((const real_constant_element_t *)v)->value,
                                     expected_real[decrement][i]));
                } else {
                    ASSERT(get_object_integer_value(r.value).value == expected_int[decrement][i]);
                    ASSERT(v->type == LATTICE_INTEGER_CONSTANT);
                    ASSERT(((const integer_constant_element_t *)v)->value
                           == expected_int[decrement][i]);
                }
                DECREF(r.value);
                instruction_t instructions[] = {{.opcode = DUP},
                                                {.opcode = decrement ? DEC : INC},
                                                {.opcode = END}};
                bytecode_t *code = make_code(instructions, 3);
                push_object_onto_stack(proc->main_thread->data_stack, operand);
                ASSERT(run(proc, code) == 0);
                collect_garbage(proc);
                ASSERT(proc->main_thread->data_stack->size == 2);
                object_t *updated = peek_object_from_stack(proc->main_thread->data_stack, 0);
                object_t *old = peek_object_from_stack(proc->main_thread->data_stack, 1);
                ASSERT(old == operand);
                if (real) {
                    ASSERT(same_real(get_object_real_value(old).value, reals[i]));
                    ASSERT(same_real(get_object_real_value(updated).value,
                                     expected_real[decrement][i]));
                } else {
                    ASSERT(get_object_integer_value(old).value == ints[i]);
                    ASSERT(get_object_integer_value(updated).value == expected_int[decrement][i]);
                }
                string_value_t text = bytecode_to_text(code);
                ASSERT(wcsstr(text.data, L"DUP") && wcsstr(text.data, decrement ? L"DEC" : L"INC"));
                FREE_STRING(text);
                free_bytecode(code);
                destroy_process(proc);
                destroy_arena(arena);
            }
        }
    }
    return true;
}

bool test_update_errors(void) {
    object_t *invalid[] = {get_null_object(),
                           get_boolean_object(true),
                           get_empty_string(),
                           get_function_print(),
                           get_exceptions_object(),
                           get_integer_proto(),
                           get_real_proto()};
    process_t *proc = create_process();
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        operation_result_t a = increment_object(proc, invalid[i]),
                           b = decrement_object(proc, invalid[i]);
        ASSERT(a.is_exception && b.is_exception);
        ASSERT(a.value == get_exception_invalid_operation() && b.value == a.value);
        DECREF(a.value);
        DECREF(b.value);
    }
    destroy_process(proc);
    for (int decrement = 0; decrement < 2; decrement++) {
        for (int caught = 0; caught < 2; caught++) {
            instruction_t instructions[] = {{.opcode = ILOAD32, .arg1 = 7},
                                            {.opcode = caught ? TRY : NOP, .arg1 = 6},
                                            {.opcode = NIL},
                                            {.opcode = DUP},
                                            {.opcode = decrement ? DEC : INC},
                                            {.opcode = END},
                                            {.opcode = END}};
            bytecode_t *code = make_code(instructions, 7);
            proc = create_process();
            ASSERT((run(proc, code) == 0) == !!caught);
            ASSERT(proc->main_thread->data_stack->size == (caught ? 2 : 0));
            ASSERT((caught ? peek_object_from_stack(proc->main_thread->data_stack, 0)
                           : proc->main_thread->exception.value)
                   == get_exception_invalid_operation());
            free_bytecode(code);
            destroy_process(proc);
        }
    }
    /* DUP retains arbitrary values too, independent of numeric update support. */
    proc = create_process();
    object_t *string = create_string_object(proc, (string_value_t){L"heap value", 10, false});
    push_object_onto_stack(proc->main_thread->data_stack, string);
    instruction_t instructions[] = {{.opcode = DUP}, {.opcode = POP}, {.opcode = END}};
    bytecode_t *code = make_code(instructions, 3);
    ASSERT(run(proc, code) == 0);
    collect_garbage(proc);
    ASSERT(peek_object_from_stack(proc->main_thread->data_stack, 0) == string);
    string_value_t text = convert_object_to_string(string);
    ASSERT(text.length == 10 && !wmemcmp(text.data, L"heap value", 10));
    FREE_STRING(text);
    free_bytecode(code);
    destroy_process(proc);
    return true;
}

bool test_update_ast_parser(void) {
    expression_t *(*factories[])(arena_t *,
                                 assignable_expression_t *) = {create_prefix_increment_node,
                                                               create_prefix_decrement_node,
                                                               create_postfix_increment_node,
                                                               create_postfix_decrement_node};
    for (size_t i = 0; i < 4; i++) {
        arena_t *arena = create_arena(8);
        variable_t *var = (variable_t *)create_variable_node(arena, (string_view_t){L"x", 1});
        /* A real declarator vtable is needed for the immutability check. */
        variable_declaration_pair_t pair =
            create_synthetic_variable_declaration_node(arena, (string_view_t){L"x", 1});
        var->declarator = pair.declarator;
        expression_t *node = factories[i](arena, &var->base);
        ASSERT(is_expression(node->base.vtbl->type));
        ASSERT(get_node_child_count(&node->base) == 1
               && get_node_child(&node->base, 0) == &var->base.base.base);
        ASSERT(!get_node_child(&node->base, 1) && !get_node_child_tag(&node->base, 1));
        ASSERT(!wcscmp(get_node_child_tag(&node->base, 0), L"operand"));
        abstract_state_t *state = create_abstract_state(arena);
        set_in_abstract_state(state, var->declarator, make_integer_constant_element(arena, 5));
        const lattice_element_t *value = calculate_expression(node, state, arena);
        ASSERT(value->type == LATTICE_INTEGER_CONSTANT);
        ASSERT(((const integer_constant_element_t *)value)->value
               == (i >= 2 ? 5 : (i == 1 ? 4 : 6)));
        value = get_from_abstract_state(state, var->declarator);
        ASSERT(((const integer_constant_element_t *)value)->value == (i % 2 ? 4 : 6));
        code_builder_t *builder = create_code_builder();
        data_builder_t *data = create_data_builder();
        generate_bytecode_from_expression(node, builder, data);
        ASSERT(builder->size == (i >= 2 ? 5 : 3));
        ASSERT(builder->instructions[0].opcode == VLOAD);
        ASSERT(builder->instructions[i >= 2 ? 2 : 1].opcode == (i % 2 ? DEC : INC));
        ASSERT(builder->instructions[i >= 2 ? 3 : 2].opcode == STORE);
        if (i >= 2)
            ASSERT(builder->instructions[1].opcode == DUP
                   && builder->instructions[4].opcode == POP);
        string_value_t source = generate_goat_code_from_expression(node);
        source_builder_t *sb = create_source_builder();
        generate_indented_goat_code_from_expression(node, sb, 0);
        string_value_t pretty = build_source(sb);
        ASSERT(pretty.length == source.length + 1
               && !wmemcmp(pretty.data, source.data, source.length));
        parser_memory_t memory = {arena, arena, arena, arena};
        ASSERT(parse_analysis_test_program(&memory, source));
        FREE_STRING(source);
        FREE_STRING(pretty);
        destroy_source_builder(sb);
        destroy_code_builder(builder);
        destroy_data_builder(data);
        destroy_abstract_state(state);
        destroy_arena(arena);
    }
    const wchar_t *valid[] = {L"var x=1; ++x",
                              L"var x=1; x--",
                              L"var x=1; ++((x))",
                              L"var x=1; (x)++",
                              L"var x=1; -x++ ** 2",
                              L"var x=1; 2 ** --x",
                              L"var x=1; x+++x",
                              L"var x=1; x++ + ++x"};
    const wchar_t *invalid[] = {L"++",
                                L"--",
                                L"++1",
                                L"1++",
                                L"++(1+2)",
                                L"(1+2)--",
                                L"++++x",
                                L"x++++",
                                L"++x++",
                                L"++-x",
                                L"f()++",
                                L"++f()",
                                L"(x=1)++",
                                L"x++=2"};
    for (size_t i = 0; i < sizeof(valid) / sizeof(*valid); i++) {
        arena_t *a = create_arena(8);
        parser_memory_t m = {a, a, a, a};
        ASSERT(
            parse_analysis_test_program(&m, (string_value_t){valid[i], wcslen(valid[i]), false}));
        destroy_arena(a);
    }
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        arena_t *a = create_arena(8);
        parser_memory_t m = {a, a, a, a};
        ASSERT(
            !parse_analysis_test_program(&m,
                                         (string_value_t){invalid[i], wcslen(invalid[i]), false}));
        destroy_arena(a);
    }
    return true;
}

bool test_update_domains(void) {
    arena_t *arena = create_arena(8);
    const lattice_element_t *range = make_integer_range_element(arena, -2, 5);
    for (int decrement = 0; decrement < 2; decrement++) {
        const integer_range_element_t *r =
            (const integer_range_element_t *)lattice_update(arena, range, decrement);
        ASSERT(r->base.type == LATTICE_INTEGER_RANGE && r->min == (decrement ? -3 : -1)
               && r->max == (decrement ? 4 : 6));
        ASSERT(lattice_update(arena, make_null_element(), decrement)->type == LATTICE_BOTTOM);
        ASSERT(lattice_update(arena, make_string_element(), decrement)->type == LATTICE_BOTTOM);
        ASSERT(lattice_update(arena, make_real_element(), decrement)->type == LATTICE_REAL);
        ASSERT(lattice_update(arena, make_numeric_element(), decrement)->type == LATTICE_NUMERIC);
        ASSERT(lattice_update(arena, make_top_element(), decrement)->type == LATTICE_TOP);
    }
    ASSERT(lattice_update(arena, make_integer_range_element(arena, INT64_MAX - 1, INT64_MAX), false)
               ->type
           == LATTICE_INTEGER_CONSTANT);
    ASSERT(lattice_update(arena, make_integer_range_element(arena, INT64_MIN, INT64_MIN + 1), true)
               ->type
           == LATTICE_INTEGER_CONSTANT);
    destroy_arena(arena);
    return true;
}
