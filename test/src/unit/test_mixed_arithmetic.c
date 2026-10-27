/** @file test_mixed_arithmetic.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent expectations for integer/real arithmetic in both operand orders.
 */
#include "analysis/addition.h"
#include "analysis/multiplication.h"
#include "analysis/subtraction.h"
#include "codegen/linker.h"
#include "model/object.h"
#include "model/process.h"
#include "model/thread.h"
#include "test_macro.h"
#include "test_multiplication.h"
#include "vm/vm.h"

#include <math.h>
#include <stdio.h>

static bool same_real(double a, double b) {
    return (isnan(a) && isnan(b)) || (a == b && (a != 0 || !!signbit(a) == !!signbit(b)));
}

bool test_mixed_arithmetic(void) {
    struct {
        int64_t integer;
        double real;
        double expected[3][2]; /* add, subtract, multiply; integer first / real first */
    } cases[] = {
        {3, 0.5, {{3.5, 3.5}, {2.5, -2.5}, {1.5, 1.5}}},
        {-3, 0.5, {{-2.5, -2.5}, {-3.5, 3.5}, {-1.5, -1.5}}},
        {3, -0.5, {{2.5, 2.5}, {3.5, -3.5}, {-1.5, -1.5}}},
        {3, 2.0, {{5.0, 5.0}, {1.0, -1.0}, {6.0, 6.0}}},
        {0, -0.0, {{0.0, 0.0}, {0.0, -0.0}, {-0.0, -0.0}}},
        {INT64_C(9007199254740993),
         1.0,
         {{9007199254740992.0, 9007199254740992.0},
          {9007199254740991.0, -9007199254740991.0},
          {9007199254740992.0, 9007199254740992.0}}},
        {INT64_MAX,
         0.5,
         {{9223372036854775808.0, 9223372036854775808.0},
          {9223372036854775808.0, -9223372036854775808.0},
          {4611686018427387904.0, 4611686018427387904.0}}},
        {INT64_MIN,
         -1.0,
         {{-9223372036854775808.0, -9223372036854775808.0},
          {-9223372036854775808.0, 9223372036854775808.0},
          {9223372036854775808.0, 9223372036854775808.0}}},
        {0, INFINITY, {{INFINITY, INFINITY}, {-INFINITY, INFINITY}, {NAN, NAN}}},
        {0, NAN, {{NAN, NAN}, {NAN, NAN}, {NAN, NAN}}},
    };

    operation_result_t (*operations[])(process_t *, object_t *, object_t *) = {add_objects,
                                                                               subtract_objects,
                                                                               multiply_objects};
    const lattice_element_t *(*abstract_operations[])(
        arena_t *,
        const lattice_element_t *,
        const lattice_element_t *) = {lattice_add, lattice_subtract, lattice_multiply};
    opcode_t opcodes[] = {ADD, SUB, MUL};
    arena_t *arena = create_arena(8);
    for (size_t op = 0; op < 3; op++) {
        code_builder_t *builder = create_code_builder();
        data_builder_t *data = create_data_builder();
        add_instruction(builder, (instruction_t){.opcode = opcodes[op]});
        add_instruction(builder, (instruction_t){.opcode = END});
        bytecode_t *code = link_code_and_data(builder, data);
        for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
            for (size_t reverse = 0; reverse < 2; reverse++) {
                process_t *proc = create_process();
                object_t *operands[2];
                operands[reverse] = create_integer_object(proc, cases[i].integer);
                operands[1 - reverse] = create_real_number_object(proc, cases[i].real);
                double expected = cases[i].expected[op][reverse];
                operation_result_t result = operations[op](proc, operands[0], operands[1]);
                ASSERT(!result.is_exception && !is_integer_object(result.value));
                ASSERT(same_real(get_object_real_value(result.value).value, expected));
                DECREF(result.value);
                const lattice_element_t *values[2];
                values[reverse] = make_integer_constant_element(arena, cases[i].integer);
                values[1 - reverse] = make_real_constant_element(arena, cases[i].real);
                const lattice_element_t *value =
                    abstract_operations[op](arena, values[0], values[1]);
                ASSERT(value->type == LATTICE_REAL_CONSTANT);
                ASSERT(same_real(((const real_constant_element_t *)value)->value, expected));
                push_object_onto_stack(proc->main_thread->data_stack, operands[0]);
                push_object_onto_stack(proc->main_thread->data_stack, operands[1]);
                ASSERT(run(proc, code) == 0);
                ASSERT(proc->main_thread->data_stack->size == 1);
                object_t *actual = pop_object_from_stack(proc->main_thread->data_stack);
                ASSERT(!is_integer_object(actual));
                ASSERT(same_real(get_object_real_value(actual).value, expected));
                DECREF(actual);
                destroy_process(proc);
            }
        }
        free_bytecode(code);
        destroy_code_builder(builder);
        destroy_data_builder(data);
    }
    destroy_arena(arena);
    return true;
}
