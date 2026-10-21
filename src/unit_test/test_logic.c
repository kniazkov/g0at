/** @file test_logic.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Defined bit patterns, truth conversion and short-circuit bytecode.
 */
#include "test_logic.h"

#include "analysis/analysis.h"
#include "analysis/bitwise.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "codegen/linker.h"
#include "graph/logic.h"
#include "lib/allocate.h"
#include "model/process.h"
#include "model/thread.h"
#include "test_macro.h"
#include "vm/vm.h"

#include <math.h>
#include <stdio.h>

static const opcode_t binary_codes[] = {BAND, BOR, BXOR, SHL, SHR};
static operation_result_t (*binary_methods[])(process_t *, object_t *, object_t *) = {
    bitwise_and_objects,
    bitwise_or_objects,
    bitwise_xor_objects,
    shift_left_objects,
    shift_right_objects};

static bytecode_t *make_code(const instruction_t *instructions, size_t count) {
    code_builder_t *cb = create_code_builder();
    data_builder_t *db = create_data_builder();
    for (size_t i = 0; i < count; i++)
        add_instruction(cb, instructions[i]);
    bytecode_t *code = link_code_and_data(cb, db);
    destroy_code_builder(cb);
    destroy_data_builder(db);
    return code;
}

bool test_bitwise_values(void) {
    struct {
        int op;
        int64_t a, b, expected;
    } cases[] = {{0, 12, 10, 8},
                 {1, 12, 10, 14},
                 {2, 12, 10, 6},
                 {0, -1, INT64_MIN, INT64_MIN},
                 {1, INT64_MIN, INT64_MAX, -1},
                 {2, -1, INT64_MIN, INT64_MAX},
                 {2, INT64_MIN, INT64_MIN, 0},
                 {3, 1, 63, INT64_MIN},
                 {3, -1, 1, -2},
                 {3, INT64_MAX, 1, -2},
                 {3, INT64_MIN, 1, 0},
                 {3, 0, 63, 0},
                 {3, -7, 0, -7},
                 {3, 3, 62, -4611686018427387904LL},
                 {4, -3, 1, -2},
                 {4, -1, 63, -1},
                 {4, INT64_MIN, 63, -1},
                 {4, INT64_MAX, 63, 0},
                 {4, INT64_MIN, 62, -2},
                 {4, 7, 1, 3},
                 {4, -7, 0, -7},
                 {4, 0, 63, 0}};

    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        process_t *proc = create_process();
        arena_t *arena = create_arena(8);
        object_t *a = create_integer_object(proc, cases[i].a),
                 *b = create_integer_object(proc, cases[i].b);
        operation_result_t r = binary_methods[cases[i].op](proc, a, b);
        ASSERT(!r.is_exception && is_integer_object(r.value)
               && get_object_integer_value(r.value).value == cases[i].expected);
        DECREF(r.value);
        const lattice_element_t *v =
            lattice_bitwise(arena,
                            make_integer_constant_element(arena, cases[i].a),
                            make_integer_constant_element(arena, cases[i].b),
                            cases[i].op);
        ASSERT(v->type == LATTICE_INTEGER_CONSTANT
               && ((const integer_constant_element_t *)v)->value == cases[i].expected);
        instruction_t instructions[] = {{.opcode = binary_codes[cases[i].op]}, {.opcode = END}};
        bytecode_t *code = make_code(instructions, 2);
        push_object_onto_stack(proc->main_thread->data_stack, a);
        push_object_onto_stack(proc->main_thread->data_stack, b);
        ASSERT(run(proc, code) == 0 && proc->main_thread->data_stack->size == 1);
        ASSERT(
            get_object_integer_value(peek_object_from_stack(proc->main_thread->data_stack, 0)).value
            == cases[i].expected);
        free_bytecode(code);
        destroy_process(proc);
        destroy_arena(arena);
    }
    int64_t a[] = {0, -1, INT64_MAX, INT64_MIN, 7, -7};
    int64_t expected[] = {-1, 0, INT64_MIN, INT64_MAX, -8, 6};
    for (size_t i = 0; i < 6; i++) {
        process_t *proc = create_process();
        object_t *value = create_integer_object(proc, a[i]);
        operation_result_t r = bitwise_not_object(proc, value);
        ASSERT(!r.is_exception && get_object_integer_value(r.value).value == expected[i]);
        DECREF(r.value);
        instruction_t instructions[] = {{.opcode = BNOT}, {.opcode = END}};
        bytecode_t *code = make_code(instructions, 2);
        push_object_onto_stack(proc->main_thread->data_stack, value);
        ASSERT(run(proc, code) == 0);
        ASSERT(
            get_object_integer_value(peek_object_from_stack(proc->main_thread->data_stack, 0)).value
            == expected[i]);
        free_bytecode(code);
        destroy_process(proc);
    }
    return true;
}

bool test_bitwise_errors(void) {
    for (int op = 0; op < 5; op++)
        for (int type = 0; type < 6; type++)
            for (int reverse = 0; reverse < 2; reverse++) {
                process_t *proc = create_process();
                object_t *integer = create_integer_object(proc, 0);
                object_t *others[] = {create_real_number_object(proc, 1.0),
                                      get_null_object(),
                                      get_boolean_object(true),
                                      get_empty_string(),
                                      get_function_print(),
                                      get_integer_proto()};
                object_t *a = reverse ? others[type] : integer,
                         *b = reverse ? integer : others[type];
                operation_result_t r = binary_methods[op](proc, a, b);
                object_t *expected =
                    reverse ? get_exception_invalid_operation() : get_exception_invalid_argument();
                ASSERT(r.is_exception && r.value == expected);
                DECREF(r.value);
                operation_result_t unary = bitwise_not_object(proc, others[type]);
                ASSERT(unary.is_exception && unary.value == get_exception_invalid_operation());
                DECREF(unary.value);
                instruction_t instructions[] = {{.opcode = binary_codes[op]}, {.opcode = END}};
                bytecode_t *code = make_code(instructions, 2);
                push_object_onto_stack(proc->main_thread->data_stack, a);
                push_object_onto_stack(proc->main_thread->data_stack, b);
                if (type != 0)
                    DECREF(others[0]);
                ASSERT(run(proc, code) != 0 && proc->main_thread->exception.value == expected);
                free_bytecode(code);
                destroy_process(proc);
            }
    int64_t counts[] = {-1, 64, INT64_MIN, INT64_MAX};
    for (int op = 3; op < 5; op++)
        for (size_t i = 0; i < 4; i++) {
            process_t *proc = create_process();
            object_t *a = create_integer_object(proc, 0),
                     *b = create_integer_object(proc, counts[i]);
            operation_result_t r = binary_methods[op](proc, a, b);
            ASSERT(r.is_exception && r.value == get_exception_invalid_argument());
            DECREF(r.value);
            instruction_t instructions[] = {{.opcode = binary_codes[op]}, {.opcode = END}};
            bytecode_t *code = make_code(instructions, 2);
            push_object_onto_stack(proc->main_thread->data_stack, a);
            push_object_onto_stack(proc->main_thread->data_stack, b);
            ASSERT(run(proc, code) != 0
                   && proc->main_thread->exception.value == get_exception_invalid_argument());
            free_bytecode(code);
            destroy_process(proc);
        }
    return true;
}

static bool contains(const lattice_element_t *value, int64_t expected) {
    if (value->type == LATTICE_INTEGER)
        return true;
    if (value->type == LATTICE_INTEGER_CONSTANT)
        return ((const integer_constant_element_t *)value)->value == expected;
    if (value->type == LATTICE_INTEGER_RANGE) {
        const integer_range_element_t *r = (const integer_range_element_t *)value;
        return expected >= r->min && expected <= r->max;
    }
    return false;
}

bool test_bitwise_domains(void) {
    arena_t *arena = create_arena(8);
    for (int lo = -4; lo <= 4; lo++)
        for (int hi = lo; hi <= 4; hi++) {
            const lattice_element_t *range = make_integer_range_element(arena, lo, hi);
            const lattice_element_t *inverted = lattice_bitwise_not(arena, range);
            for (int x = lo; x <= hi; x++)
                ASSERT(contains(inverted, -1 - x));
            for (int op = 0; op < 5; op++)
                for (int k = 0; k <= 3; k++) {
                    const lattice_element_t *v =
                        lattice_bitwise(arena, range, make_integer_constant_element(arena, k), op);
                    for (int x = lo; x <= hi; x++) {
                        int64_t expected = op == 0   ? (x & k)
                                           : op == 1 ? (x | k)
                                           : op == 2 ? (x ^ k)
                                           : op == 3 ? x * (1 << k)
                                                     : (int64_t)floor((double)x / (1 << k));
                        ASSERT(contains(v, expected));
                    }
                }
        }
    for (int op = 0; op < 5; op++) {
        ASSERT(lattice_bitwise(arena, make_real_element(), make_integer_element(), op)->type
               == LATTICE_BOTTOM);
        ASSERT(lattice_bitwise(arena,
                               make_integer_constant_element(arena, 0),
                               make_string_element(),
                               op)
                   ->type
               == LATTICE_BOTTOM);
        ASSERT(lattice_bitwise(arena, make_top_element(), make_top_element(), op)->type
               == LATTICE_INTEGER);
    }
    ASSERT(lattice_bitwise(arena,
                           make_integer_constant_element(arena, 0),
                           make_integer_range_element(arena, 64, 99),
                           BIT_SHIFT_LEFT)
               ->type
           == LATTICE_BOTTOM);
    ASSERT(lattice_bitwise(arena,
                           make_integer_constant_element(arena, 0),
                           make_integer_range_element(arena, -1, 1),
                           BIT_SHIFT_LEFT)
               ->type
           == LATTICE_INTEGER_CONSTANT);
    ASSERT(lattice_boolean(make_bottom_element(), false)->type == LATTICE_BOTTOM);
    ASSERT(lattice_boolean(make_top_element(), true)->type == LATTICE_BOOLEAN);
    destroy_arena(arena);
    return true;
}

bool test_logical_vm(void) {
    for (int kind = 0; kind < 8; kind++)
        for (int op = 0; op < 4; op++) {
            process_t *proc = create_process();
            arena_t *arena = create_arena(8);
            object_t *values[] = {get_null_object(),
                                  get_boolean_object(false),
                                  get_boolean_object(true),
                                  create_integer_object(proc, 0),
                                  create_integer_object(proc, 9000),
                                  create_real_number_object(proc, NAN),
                                  create_string_object(proc, STATIC_STRING(L"heap string")),
                                  get_function_print()};
            bool truth[] = {false, false, true, false, true, true, true, true};
            opcode_t codes[] = {LNOT, BOOL, LAND, LOR};
            /* The short-circuit path skips both a throw and the final conversion. */
            instruction_t instructions[] = {{.opcode = codes[op], .arg1 = 3},
                                            {.opcode = NIL},
                                            {.opcode = THROW},
                                            {.opcode = END}};
            if (op < 2)
                instructions[1].opcode = END;
            bytecode_t *code = make_code(instructions, 4);
            push_object_onto_stack(proc->main_thread->data_stack, values[kind]);
            bool throws = op >= 2 && truth[kind] != (op == 3);
            for (int i = 0; i < 8; i++)
                if (i != kind)
                    DECREF(values[i]);
            ASSERT((run(proc, code) != 0) == throws);
            if (!throws) {
                ASSERT(peek_object_from_stack(proc->main_thread->data_stack, 0)
                       == get_boolean_object(op == 0 ? !truth[kind] : truth[kind]));
            } else {
                ASSERT(proc->main_thread->exception.value == get_null_object());
            }
            free_bytecode(code);
            destroy_process(proc);
            destroy_arena(arena);
        }
    return true;
}

bool test_logic_nodes(void) {
    expression_t *(*unary[])(arena_t *, expression_t *) = {create_logical_not_node,
                                                           create_boolean_conversion_node,
                                                           create_bitwise_not_node};
    expression_t *(*binary[])(arena_t *, expression_t *, expression_t *) = {
        create_logical_and_node,
        create_logical_or_node,
        create_bitwise_and_node,
        create_bitwise_or_node,
        create_bitwise_xor_node,
        create_shift_left_node,
        create_shift_right_node};
    for (int op = 0; op < 10; op++) {
        arena_t *arena = create_arena(8);
        expression_t *a = (expression_t *)create_integer_node(arena, 1),
                     *b = (expression_t *)create_integer_node(arena, 2);
        expression_t *expr = op < 3 ? unary[op](arena, a) : binary[op - 3](arena, a, b);
        ASSERT(get_node_child_count(&expr->base) == (op < 3 ? 1 : 2));
        ASSERT(get_node_child(&expr->base, 0) == &a->base && !get_node_child(&expr->base, 2));
        string_value_t source = generate_goat_code_from_expression(expr);
        parser_memory_t memory = {arena, arena, arena, arena};
        ASSERT(parse_analysis_test_program(&memory, source));
        FREE_STRING(source);
        code_builder_t *cb = create_code_builder();
        data_builder_t *db = create_data_builder();
        generate_bytecode_from_expression(expr, cb, db);
        add_instruction(cb, (instruction_t){.opcode = END});
        bytecode_t *code = link_code_and_data(cb, db);
        process_t *proc = create_process();
        ASSERT(run(proc, code) == 0 && proc->main_thread->data_stack->size == 1);
        const wchar_t *names[] =
            {L"LNOT", L"BOOL", L"BNOT", L"LAND", L"LOR", L"BAND", L"BOR", L"BXOR", L"SHL", L"SHR"};
        string_value_t dump = bytecode_to_text(code);
        ASSERT(wcsstr(dump.data, names[op]));
        FREE_STRING(dump);
        int64_t expected[] = {0, 1, -2, 1, 1, 0, 3, 3, 4, 0};
        object_t *value = peek_object_from_stack(proc->main_thread->data_stack, 0);
        if (op < 2 || op == 3 || op == 4) {
            ASSERT(value == get_boolean_object(expected[op]));
        } else {
            ASSERT(get_object_integer_value(value).value == expected[op]);
        }
        free_bytecode(code);
        destroy_code_builder(cb);
        destroy_data_builder(db);
        destroy_process(proc);
        destroy_arena(arena);
    }
    for (int disabled = 0; disabled < 2; disabled++) {
        arena_t *arena = create_arena(8);
        parser_memory_t memory = {arena, arena, arena, arena};
        node_t *root =
            parse_analysis_test_program(&memory, STATIC_STRING(L"var x=0;false&&++x;true||++x;"));
        ASSERT(root);
        options_t *options = create_options();
        options->optimization_level = disabled ? OPTIMIZATION_NONE : OPTIMIZATION_ALL;
        ASSERT(!analyze(root, &memory, options, NULL));
        code_builder_t *cb = create_code_builder();
        data_builder_t *db = create_data_builder();
        generate_bytecode_from_node(root, cb, db);
        size_t increments = 0, branches = 0;
        for (size_t i = 0; i < cb->size; i++) {
            increments += cb->instructions[i].opcode == INC;
            branches += cb->instructions[i].opcode == LAND || cb->instructions[i].opcode == LOR;
        }
        ASSERT(increments == (disabled ? 2 : 0) && branches == (disabled ? 2 : 0));
        destroy_code_builder(cb);
        destroy_data_builder(db);
        destroy_options(options);
        destroy_arena(arena);
    }
    const wchar_t *invalid[] =
        {L"!", L"~", L"1 &&", L"|| 1", L"1 <<", L"1 &", L"1 ! 2", L"1 ~~ 2", L"1 <<< 2"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        arena_t *arena = create_arena(8);
        parser_memory_t m = {arena, arena, arena, arena};
        ASSERT(
            !parse_analysis_test_program(&m,
                                         (string_value_t){invalid[i], wcslen(invalid[i]), false}));
        destroy_arena(arena);
    }
    return true;
}
