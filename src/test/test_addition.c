/** @file test_addition.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Differential and boundary tests for addition.
 */
#include <stdio.h>
#include <math.h>
#include <float.h>
#include "test_addition.h"
#include "test_macro.h"
#include "lib/allocate.h"
#include "analysis/addition.h"
#include "model/object.h"
#include "model/process.h"
#include "model/thread.h"
#include "codegen/linker.h"
#include "vm/vm.h"

/** @brief Compares real results including NaN and the sign of zero. */
static bool same_real(double a, double b) {
    return (isnan(a) && isnan(b)) || (a == b && (a != 0 || !!signbit(a) == !!signbit(b)));
}

/** @brief Checks a concrete result against a constant or broad abstract value. */
static bool contains(const lattice_element_t *value, object_t *object) {
    if (!object) return value->type == LATTICE_BOTTOM;
    if (value->type == LATTICE_TOP || value->type == LATTICE_NOT_NULL) return true;
    if (is_integer_object(object)) {
        int64_t n = get_object_integer_value(object).value;
        if (value->type == LATTICE_NUMERIC || value->type == LATTICE_INTEGER) return true;
        if (value->type == LATTICE_INTEGER_CONSTANT)
            return ((const integer_constant_element_t*)value)->value == n;
        if (value->type == LATTICE_INTEGER_RANGE) {
            const integer_range_element_t *r = (const integer_range_element_t*)value;
            return r->min <= n && n <= r->max;
        }
        return false;
    }
    real_value_t real = get_object_real_value(object);
    if (real.has_value) {
        if (value->type == LATTICE_NUMERIC || value->type == LATTICE_REAL) return true;
        return value->type == LATTICE_REAL_CONSTANT &&
            same_real(((const real_constant_element_t*)value)->value, real.value);
    }
    if (object->vtbl->type == TYPE_STRING) {
        if (value->type == LATTICE_STRING) return true;
        if (value->type != LATTICE_STRING_CONSTANT) return false;
        string_view_t expected = ((const string_constant_element_t*)value)->value;
        string_value_t actual = convert_object_to_string(object);
        bool equal = expected.length == actual.length &&
            !wmemcmp(expected.data, actual.data, actual.length);
        FREE_STRING(actual);
        return equal;
    }
    return false;
}

bool test_addition_models_and_constants(void) {
    arena_t *arena = create_arena(8);
    process_t *proc = create_process();
    struct { object_t *object; const lattice_element_t *value; } cases[40];
    size_t count = 0;
    int64_t integers[] = {0, 1, -1, 128, -129, INT64_MIN, INT64_MAX,
        INT64_C(9007199254740993)};
    for (size_t i = 0; i < sizeof(integers)/sizeof(*integers); i++) {
        cases[count].object = create_integer_object(proc, integers[i]);
        cases[count++].value = make_integer_constant_element(arena, integers[i]);
    }
    double reals[] = {0.0, -0.0, 0.5, -1.25, DBL_MAX, DBL_MIN, INFINITY, -INFINITY, NAN};
    for (size_t i = 0; i < sizeof(reals)/sizeof(*reals); i++) {
        cases[count].object = create_real_number_object(proc, reals[i]);
        cases[count++].value = make_real_constant_element(arena, reals[i]);
    }
    const wchar_t *strings[] = {L"", L"x", L"a b", L"\u0416\U0001F410"};
    for (size_t i = 0; i < sizeof(strings)/sizeof(*strings); i++) {
        cases[count].object = create_string_object(proc,
            (string_value_t){.data = strings[i], .length = wcslen(strings[i]), .should_free = false});
        cases[count++].value = make_string_constant_element(arena,
            (string_view_t){strings[i], wcslen(strings[i])});
    }
    cases[count].object = get_null_object();
    cases[count++].value = make_null_element();
    cases[count].object = get_boolean_object(true);
    cases[count++].value = make_true_element();
    cases[count].object = get_boolean_object(false);
    cases[count++].value = make_false_element();
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    add_instruction(builder, (instruction_t){.opcode = ADD});
    add_instruction(builder, (instruction_t){.opcode = END});
    bytecode_t *code = link_code_and_data(builder, data);
    /* Keep the test inputs rooted across run()'s garbage collection. */
    for (size_t i = 0; i < count; i++) {
        INCREF(cases[i].object);
        push_object_onto_stack(proc->main_thread->data_stack, cases[i].object);
    }
    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < count; j++) {
            object_t *result = add_objects(proc, cases[i].object, cases[j].object);
            const lattice_element_t *value = lattice_add(arena, cases[i].value, cases[j].value);
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
    /* Independent expectations: differential agreement alone can preserve shared bugs. */
    object_t *half = create_real_number_object(proc, 0.5);
    object_t *sum = add_objects(proc, get_static_integer_object(1), half);
    ASSERT(!is_integer_object(sum));
    ASSERT(get_object_real_value(sum).value == 1.5);
    DECREF(sum);
    DECREF(half);
    const lattice_element_t *max = make_integer_constant_element(arena, INT64_MAX);
    const lattice_element_t *min = make_integer_constant_element(arena, INT64_MIN);
    const lattice_element_t *one = make_integer_constant_element(arena, 1);
    const lattice_element_t *minus_one = make_integer_constant_element(arena, -1);
    ASSERT(((const integer_constant_element_t*)lattice_add(arena, max, one))->value == INT64_MIN);
    ASSERT(((const integer_constant_element_t*)lattice_add(arena, min, minus_one))->value == INT64_MAX);
    ASSERT(((const integer_constant_element_t*)lattice_add(arena, min, min))->value == 0);
    for (size_t i = 0; i < count; i++) DECREF(cases[i].object);
    free_bytecode(code);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    destroy_process(proc);
    destroy_arena(arena);
    return true;
}

bool test_addition_domains(void) {
    arena_t *arena = create_arena(4);
    const lattice_element_t *domains[] = {
        make_top_element(), make_not_null_element(), make_bottom_element(),
        make_null_element(), make_boolean_element(), make_true_element(), make_false_element(),
        make_function_element(), make_user_defined_object_element(),
        make_numeric_element(), make_integer_element(),
        make_integer_range_element(arena, -2, 3), make_integer_constant_element(arena, 2),
        make_real_element(), make_real_constant_element(arena, 0.5),
        make_string_element(), make_string_constant_element(arena, (string_view_t){L"x", 1}),
        make_array_element(), make_typed_array_element(arena, LATTICE_INTEGER)
    };
    for (size_t i = 0; i < sizeof(domains)/sizeof(*domains); i++) {
        for (size_t j = 0; j < sizeof(domains)/sizeof(*domains); j++) {
            const lattice_element_t *a = domains[i], *b = domains[j];
            const lattice_element_t *r = lattice_add(arena, a, b);
            if (a->type == LATTICE_BOTTOM || b->type == LATTICE_BOTTOM ||
                a->type == LATTICE_NULL || is_boolean_lattice_element(a) ||
                a->type == LATTICE_FUNCTION || a->type == LATTICE_USER_DEFINED_OBJECT) {
                ASSERT(r->type == LATTICE_BOTTOM);
            } else if (is_string_lattice_element(a)) {
                ASSERT(is_string_lattice_element(r));
            } else if (is_numeric_lattice_element(a)) {
                if (is_numeric_lattice_element(b) || b->type == LATTICE_TOP ||
                    b->type == LATTICE_NOT_NULL) {
                    ASSERT(is_numeric_lattice_element(r));
                } else {
                    ASSERT(r->type == LATTICE_BOTTOM);
                }
            } else {
                ASSERT(r->type == LATTICE_TOP);
            }
        }
    }
    ASSERT(lattice_add(arena, make_integer_element(), make_real_element())->type == LATTICE_REAL);
    ASSERT(lattice_add(arena, make_real_element(), make_integer_element())->type == LATTICE_REAL);
    ASSERT(lattice_add(arena, make_integer_element(), make_top_element())->type == LATTICE_NUMERIC);
    destroy_arena(arena);
    return true;
}

bool test_addition_ranges(void) {
    arena_t *arena = create_arena(8);
    process_t *proc = create_process();
    int64_t intervals[][2] = {{-3, 2}, {0, 0}, {1, 3}, {INT64_MAX-2, INT64_MAX},
        {INT64_MIN, INT64_MIN+2}, {-1, 1}};
    for (size_t i = 0; i < sizeof(intervals)/sizeof(*intervals); i++) {
        for (size_t j = 0; j < sizeof(intervals)/sizeof(*intervals); j++) {
            const lattice_element_t *a = make_integer_range_element(arena, intervals[i][0], intervals[i][1]);
            const lattice_element_t *b = make_integer_range_element(arena, intervals[j][0], intervals[j][1]);
            const lattice_element_t *r = lattice_add(arena, a, b);
            for (int64_t x = intervals[i][0]; ; x++) {
                for (int64_t y = intervals[j][0]; ; y++) {
                    object_t *left = create_integer_object(proc, x);
                    object_t *right = create_integer_object(proc, y);
                    object_t *sum = add_objects(proc, left, right);
                    ASSERT(contains(r, sum));
                    DECREF(left); DECREF(right); DECREF(sum);
                    if (y == intervals[j][1]) break;
                }
                if (x == intervals[i][1]) break;
            }
        }
    }
    const lattice_element_t *r = lattice_add(arena,
        make_integer_range_element(arena, -3, 2), make_integer_range_element(arena, 1, 3));
    ASSERT(r->type == LATTICE_INTEGER_RANGE);
    ASSERT(((const integer_range_element_t*)r)->min == -2);
    ASSERT(((const integer_range_element_t*)r)->max == 5);
    r = lattice_add(arena, make_integer_range_element(arena, INT64_MAX-1, INT64_MAX),
        make_integer_constant_element(arena, 1));
    ASSERT(r->type == LATTICE_INTEGER);
    destroy_process(proc);
    destroy_arena(arena);
    return true;
}

static size_t released_operands;

/** @brief Records VM ownership releases independently of garbage collection. */
static void count_release(object_t *object) {
    released_operands++;
}

bool test_addition_vm_errors(void) {
    /* Invalid types and both forms of stack underflow must fail and consume operands. */
    for (int operands = 0; operands <= 2; operands++) {
        process_t *proc = create_process();
        code_builder_t *builder = create_code_builder();
        data_builder_t *data = create_data_builder();
        object_vtbl_t tracked_vtbl = *get_boolean_object(true)->vtbl;
        tracked_vtbl.dec_ref = count_release;
        object_t tracked = *get_boolean_object(true);
        tracked.vtbl = &tracked_vtbl;
        released_operands = 0;
        for (int i = 0; i < operands; i++)
            push_object_onto_stack(proc->main_thread->data_stack, &tracked);
        add_instruction(builder, (instruction_t){.opcode = ADD});
        add_instruction(builder, (instruction_t){.opcode = END});
        bytecode_t *code = link_code_and_data(builder, data);
        ASSERT(run(proc, code) != 0);
        ASSERT(proc->main_thread->data_stack->size == 0);
        ASSERT(released_operands == (size_t)operands);
        destroy_process(proc);
        free_bytecode(code);
        destroy_code_builder(builder);
        destroy_data_builder(data);
    }
    return true;
}
