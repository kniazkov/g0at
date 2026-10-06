/** @file test_division.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Differential and boundary tests for division.
 */
#include "test_division.h"

#include "analysis/division.h"
#include "codegen/linker.h"
#include "lib/allocate.h"
#include "model/object.h"
#include "model/process.h"
#include "model/thread.h"
#include "test_macro.h"
#include "vm/vm.h"

#include <float.h>
#include <math.h>
#include <stdio.h>

/** @brief Compares real results including NaN and the sign of zero. */
static bool same_real(double a, double b) {
    return (isnan(a) && isnan(b)) || (a == b && (a != 0 || !!signbit(a) == !!signbit(b)));
}

/** @brief Checks a concrete result against a constant or broad abstract value. */
static bool contains(const lattice_element_t *value, object_t *object) {
    if (!object)
        return value->type == LATTICE_BOTTOM;
    if (value->type == LATTICE_TOP || value->type == LATTICE_NOT_NULL)
        return true;
    if (is_integer_object(object)) {
        int64_t n = get_object_integer_value(object).value;
        if (value->type == LATTICE_NUMERIC || value->type == LATTICE_INTEGER)
            return true;
        if (value->type == LATTICE_INTEGER_CONSTANT)
            return ((const integer_constant_element_t *)value)->value == n;
        if (value->type == LATTICE_INTEGER_RANGE) {
            const integer_range_element_t *r = (const integer_range_element_t *)value;
            return r->min <= n && n <= r->max;
        }
        return false;
    }
    real_value_t real = get_object_real_value(object);
    if (real.has_value) {
        if (value->type == LATTICE_NUMERIC || value->type == LATTICE_REAL)
            return true;
        return value->type == LATTICE_REAL_CONSTANT
               && same_real(((const real_constant_element_t *)value)->value, real.value);
    }
    if (object->vtbl->type == TYPE_STRING) {
        if (value->type == LATTICE_STRING)
            return true;
        if (value->type != LATTICE_STRING_CONSTANT)
            return false;
        string_view_t expected = ((const string_constant_element_t *)value)->value;
        string_value_t actual = convert_object_to_string(object);
        bool equal =
            expected.length == actual.length && !wmemcmp(expected.data, actual.data, actual.length);
        FREE_STRING(actual);
        return equal;
    }
    return false;
}

bool test_division_models_and_constants(void) {
    arena_t *arena = create_arena(8);
    process_t *proc = create_process();

    struct {
        object_t *object;
        const lattice_element_t *value;
    } cases[40];

    size_t count = 0;
    int64_t integers[] = {0, 1, -1, 128, -129, INT64_MIN, INT64_MAX, INT64_C(9007199254740993)};
    for (size_t i = 0; i < sizeof(integers) / sizeof(*integers); i++) {
        cases[count].object = create_integer_object(proc, integers[i]);
        cases[count++].value = make_integer_constant_element(arena, integers[i]);
    }
    double reals[] = {0.0, -0.0, 0.5, -1.25, DBL_MAX, DBL_MIN, INFINITY, -INFINITY, NAN};
    for (size_t i = 0; i < sizeof(reals) / sizeof(*reals); i++) {
        cases[count].object = create_real_number_object(proc, reals[i]);
        cases[count++].value = make_real_constant_element(arena, reals[i]);
    }
    const wchar_t *strings[] = {L"", L"x", L"a b", L"\u0416\U0001F410"};
    for (size_t i = 0; i < sizeof(strings) / sizeof(*strings); i++) {
        cases[count].object = create_string_object(proc,
                                                   (string_value_t){.data = strings[i],
                                                                    .length = wcslen(strings[i]),
                                                                    .should_free = false});
        cases[count++].value =
            make_string_constant_element(arena, (string_view_t){strings[i], wcslen(strings[i])});
    }
    cases[count].object = get_null_object();
    cases[count++].value = make_null_element();
    cases[count].object = get_boolean_object(true);
    cases[count++].value = make_true_element();
    cases[count].object = get_boolean_object(false);
    cases[count++].value = make_false_element();
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    add_instruction(builder, (instruction_t){.opcode = DIVIDE});
    add_instruction(builder, (instruction_t){.opcode = END});
    bytecode_t *code = link_code_and_data(builder, data);
    /* Keep the test inputs rooted across run()'s garbage collection. */
    for (size_t i = 0; i < count; i++) {
        INCREF(cases[i].object);
        push_object_onto_stack(proc->main_thread->data_stack, cases[i].object);
    }
    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < count; j++) {
            operation_result_t outcome = divide_objects(proc, cases[i].object, cases[j].object);
            ASSERT(outcome.value);
            object_t *result = outcome.is_exception ? NULL : outcome.value;
            if (outcome.is_exception) {
                ASSERT(outcome.value == get_exception_invalid_argument()
                       || outcome.value == get_exception_invalid_operation()
                       || outcome.value == get_exception_division_by_zero());
                DECREF(outcome.value);
            }
            const lattice_element_t *value = lattice_divide(arena, cases[i].value, cases[j].value);
            if (!contains(value, result))
                printf("Division mismatch for operand indices %zu, %zu\n", i, j);
            ASSERT(contains(value, result));
            if (result) {
                DECREF(result);
                INCREF(cases[i].object);
                INCREF(cases[j].object);
                push_object_onto_stack(proc->main_thread->data_stack, cases[i].object);
                push_object_onto_stack(proc->main_thread->data_stack, cases[j].object);
                proc->main_thread->instr_id = 0;
                ASSERT(run(proc, code) == 0);
                ASSERT(proc->main_thread->data_stack->size == count + 1);
                result = pop_object_from_stack(proc->main_thread->data_stack);
                ASSERT(contains(value, result));
                DECREF(result);
            }
        }
    }
    for (size_t i = 0; i < count; i++)
        DECREF(cases[i].object);
    free_bytecode(code);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    destroy_process(proc);
    destroy_arena(arena);
    return true;
}

bool test_division_domains(void) {
    arena_t *arena = create_arena(8);
    const lattice_element_t *domains[] = {
        make_top_element(),
        make_not_null_element(),
        make_bottom_element(),
        make_null_element(),
        make_boolean_element(),
        make_true_element(),
        make_false_element(),
        make_function_element(),
        make_user_defined_object_element(),
        make_numeric_element(),
        make_integer_element(),
        make_integer_range_element(arena, -2, 3),
        make_integer_constant_element(arena, 2),
        make_integer_constant_element(arena, 0),
        make_real_constant_element(arena, -0.0),
        make_real_element(),
        make_real_constant_element(arena, 0.5),
        make_string_element(),
        make_string_constant_element(arena, (string_view_t){L"x", 1}),
        make_array_element(),
        make_typed_array_element(arena, LATTICE_INTEGER)};
    for (size_t i = 0; i < sizeof(domains) / sizeof(*domains); i++) {
        for (size_t j = 0; j < sizeof(domains) / sizeof(*domains); j++) {
            const lattice_element_t *a = domains[i], *b = domains[j];
            const lattice_element_t *r = lattice_divide(arena, a, b);
            if (a->type == LATTICE_BOTTOM || b->type == LATTICE_BOTTOM) {
                ASSERT(r->type == LATTICE_BOTTOM);
            } else if (a->type == LATTICE_TOP || a->type == LATTICE_NOT_NULL
                       || a->type == LATTICE_ARRAY || a->type == LATTICE_TYPED_ARRAY) {
                ASSERT(r->type == LATTICE_TOP);
            } else if (!is_numeric_lattice_element(a)
                       || (!is_numeric_lattice_element(b) && b->type != LATTICE_TOP
                           && b->type != LATTICE_NOT_NULL)) {
                ASSERT(r->type == LATTICE_BOTTOM);
            } else if ((b->type == LATTICE_INTEGER_CONSTANT
                        && ((const integer_constant_element_t *)b)->value == 0)
                       || (b->type == LATTICE_REAL_CONSTANT
                           && ((const real_constant_element_t *)b)->value == 0)) {
                ASSERT(r->type == LATTICE_BOTTOM);
            } else if (is_real_lattice_element(a) || is_real_lattice_element(b)) {
                ASSERT(is_real_lattice_element(r));
            } else if (a->type == LATTICE_NUMERIC || b->type == LATTICE_NUMERIC
                       || b->type == LATTICE_TOP || b->type == LATTICE_NOT_NULL) {
                ASSERT(r->type == LATTICE_NUMERIC);
            } else {
                ASSERT(is_numeric_lattice_element(r));
            }
        }
    }
    destroy_arena(arena);
    return true;
}

bool test_division_ranges(void) {
    arena_t *arena = create_arena(8);
    process_t *proc = create_process();
    int64_t intervals[][2] =
        {{-3, 2}, {0, 0}, {1, 3}, {INT64_MAX - 2, INT64_MAX}, {INT64_MIN, INT64_MIN + 2}, {-1, 1}};
    for (size_t i = 0; i < sizeof(intervals) / sizeof(*intervals); i++) {
        for (size_t j = 0; j < sizeof(intervals) / sizeof(*intervals); j++) {
            for (int constant = 0; constant < 3; constant++) {
                const lattice_element_t *a =
                    constant == 1
                        ? make_integer_constant_element(arena, intervals[i][0])
                        : make_integer_range_element(arena, intervals[i][0], intervals[i][1]);
                const lattice_element_t *b =
                    constant == 2
                        ? make_integer_constant_element(arena, intervals[j][0])
                        : make_integer_range_element(arena, intervals[j][0], intervals[j][1]);
                const lattice_element_t *r = lattice_divide(arena, a, b);
                for (int64_t x = intervals[i][0];; x++) {
                    for (int64_t y = intervals[j][0];; y++) {
                        object_t *left = create_integer_object(proc, x);
                        object_t *right = create_integer_object(proc, y);
                        operation_result_t result = divide_objects(proc, left, right);
                        if (result.is_exception) {
                            ASSERT(result.value == get_exception_division_by_zero());
                        } else {
                            ASSERT(contains(r, result.value));
                        }
                        DECREF(left);
                        DECREF(right);
                        DECREF(result.value);
                        if (constant == 2 || y == intervals[j][1])
                            break;
                    }
                    if (constant == 1 || x == intervals[i][1])
                        break;
                }
            }
        }
    }
    const lattice_element_t *range = make_integer_range_element(arena, -3, 2);
    ASSERT(lattice_divide(arena, range, make_integer_constant_element(arena, 1)) == range);
    const integer_range_element_t *negated =
        (const integer_range_element_t *)lattice_divide(arena,
                                                        range,
                                                        make_integer_constant_element(arena, -1));
    ASSERT(negated->base.type == LATTICE_INTEGER_RANGE && negated->min == -2 && negated->max == 3);
    ASSERT(lattice_divide(arena, range, make_integer_constant_element(arena, 2))->type
           == LATTICE_NUMERIC);
    ASSERT(lattice_divide(arena,
                          make_integer_range_element(arena, INT64_MIN, INT64_MIN + 2),
                          make_integer_constant_element(arena, -1))
               ->type
           == LATTICE_INTEGER_RANGE);
    ASSERT(lattice_divide(arena, make_integer_constant_element(arena, 0), range)->type
           == LATTICE_INTEGER_CONSTANT);
    ASSERT(lattice_divide(arena, range, make_integer_constant_element(arena, 0))->type
           == LATTICE_BOTTOM);
    ASSERT(lattice_divide(arena, make_real_element(), make_real_constant_element(arena, -0.0))->type
           == LATTICE_BOTTOM);
    destroy_process(proc);
    destroy_arena(arena);
    return true;
}

bool test_division_expected(void) {
    struct {
        int64_t left, right;
        bool integer;
        int64_t expected_int;
        double expected_real;
    } integers[] = {
        {INT64_MAX, 1, true, INT64_MAX, 0},
        {INT64_MIN, 1, true, INT64_MIN, 0},
        {INT64_MAX, -1, true, -INT64_MAX, 0},
        {INT64_MIN, -1, true, INT64_MAX, 0},
        {INT64_MIN, INT64_MIN, true, 1, 0},
        {INT64_MAX, INT64_MAX, true, 1, 0},
        {9007199254740993LL, 3, true, 3002399751580331LL, 0},
        {INT64_MAX - 1, 2, true, (INT64_MAX - 1) / 2, 0},
        {INT64_MAX, 2, false, 0, 0x1p62},
        {5, 2, false, 0, 2.5},
        {-5, 2, false, 0, -2.5},
        {5, -2, false, 0, -2.5},
        {-5, -2, false, 0, 2.5},
        {0, -3, true, 0, 0},
        {1, 3, false, 0, 1.0 / 3.0},
        {INT64_MIN, 0, false, 0, 0},
        {0, 0, false, 0, 0},
    };

    struct {
        int64_t integer;
        double real, expected[2];
        bool exception[2];
    } mixed[] = {
        {3, 0.5, {6.0, 1.0 / 6.0}, {false, false}},
        {3, 2.0, {1.5, 2.0 / 3.0}, {false, false}},
        {4, 2.0, {2.0, 0.5}, {false, false}},
        {-3, 0.5, {-6.0, -1.0 / 6.0}, {false, false}},
        {1, INFINITY, {0.0, INFINITY}, {false, false}},
        {1, -INFINITY, {-0.0, -INFINITY}, {false, false}},
        {INT64_MAX, 1.0, {0x1p63, 0x1p-63}, {false, false}},
        {INT64_MIN, -1.0, {0x1p63, 0x1p-63}, {false, false}},
        {9007199254740993LL, 1.0, {0x1p53, 0x1p-53}, {false, false}},
        {1, -0.0, {0, -0.0}, {true, false}},
        {0, 1.0, {0.0, 0}, {false, true}},
        {0, -1.0, {-0.0, 0}, {false, true}},
        {0, NAN, {NAN, 0}, {false, true}},
        {1, NAN, {NAN, NAN}, {false, false}},
    };

    struct {
        double left, right, expected;
        bool exception;
    } reals[] = {
        {1.0, 0.0, 0, true},
        {1.0, -0.0, 0, true},
        {NAN, 0.0, 0, true},
        {-0.0, 1.0, -0.0, false},
        {0.0, -1.0, -0.0, false},
        {INFINITY, INFINITY, NAN, false},
        {0.0, INFINITY, 0.0, false},
        {DBL_MAX, DBL_MIN, INFINITY, false},
        {DBL_MIN, 2.0, DBL_MIN / 2, false},
        {DBL_MIN, DBL_MAX, 0.0, false},
        {1.0, NAN, NAN, false},
    };

    arena_t *arena = create_arena(8);
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    add_instruction(builder, (instruction_t){.opcode = DIVIDE});
    add_instruction(builder, (instruction_t){.opcode = END});
    bytecode_t *code = link_code_and_data(builder, data);
    size_t ni = sizeof(integers) / sizeof(*integers);
    size_t nm = sizeof(mixed) / sizeof(*mixed);
    size_t nr = sizeof(reals) / sizeof(*reals);
    for (size_t i = 0; i < ni + 2 * nm + nr; i++) {
        process_t *proc = create_process();
        object_t *a, *b;
        const lattice_element_t *av, *bv;
        bool integer = false, exception = false;
        int64_t expected_int = 0;
        double expected_real = 0;
        if (i < ni) {
            a = create_integer_object(proc, integers[i].left);
            b = create_integer_object(proc, integers[i].right);
            av = make_integer_constant_element(arena, integers[i].left);
            bv = make_integer_constant_element(arena, integers[i].right);
            integer = integers[i].integer;
            exception = integers[i].right == 0;
            expected_int = integers[i].expected_int;
            expected_real = integers[i].expected_real;
        } else if (i < ni + 2 * nm) {
            size_t j = (i - ni) / 2, reverse = (i - ni) % 2;
            a = create_integer_object(proc, mixed[j].integer);
            b = create_real_number_object(proc, mixed[j].real);
            av = make_integer_constant_element(arena, mixed[j].integer);
            bv = make_real_constant_element(arena, mixed[j].real);
            if (reverse) {
                object_t *temp = a;
                a = b;
                b = temp;
                const lattice_element_t *v = av;
                av = bv;
                bv = v;
            }
            exception = mixed[j].exception[reverse];
            expected_real = mixed[j].expected[reverse];
        } else {
            size_t j = i - ni - 2 * nm;
            a = create_real_number_object(proc, reals[j].left);
            b = create_real_number_object(proc, reals[j].right);
            av = make_real_constant_element(arena, reals[j].left);
            bv = make_real_constant_element(arena, reals[j].right);
            exception = reals[j].exception;
            expected_real = reals[j].expected;
        }
        const lattice_element_t *value = lattice_divide(arena, av, bv);
        if (exception) {
            ASSERT(value->type == LATTICE_BOTTOM);
        } else if (integer) {
            ASSERT(value->type == LATTICE_INTEGER_CONSTANT);
            ASSERT(((const integer_constant_element_t *)value)->value == expected_int);
        } else {
            ASSERT(value->type == LATTICE_REAL_CONSTANT);
            ASSERT(same_real(((const real_constant_element_t *)value)->value, expected_real));
        }
        operation_result_t result = divide_objects(proc, a, b);
        ASSERT(result.is_exception == exception);
        if (exception) {
            ASSERT(result.value == get_exception_division_by_zero());
        } else {
            ASSERT(is_integer_object(result.value) == integer);
            ASSERT(contains(value, result.value));
        }
        DECREF(result.value);
        push_object_onto_stack(proc->main_thread->data_stack, a);
        push_object_onto_stack(proc->main_thread->data_stack, b);
        ASSERT((run(proc, code) != 0) == exception);
        if (exception) {
            ASSERT(proc->main_thread->exception.value == get_exception_division_by_zero());
            ASSERT(proc->main_thread->data_stack->size == 0);
        } else {
            ASSERT(proc->main_thread->data_stack->size == 1);
            object_t *actual = pop_object_from_stack(proc->main_thread->data_stack);
            ASSERT(contains(value, actual));
            DECREF(actual);
        }
        destroy_process(proc);
    }
    free_bytecode(code);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    destroy_arena(arena);
    return true;
}
