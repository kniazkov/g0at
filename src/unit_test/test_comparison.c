/** @file test_comparison.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Comparisons across the model, VM, lattice and parser.
 */
#include "test_comparison.h"

#include "analysis/comparison.h"
#include "analysis_test_support.h"
#include "codegen/linker.h"
#include "codegen/source_builder.h"
#include "graph/comparison.h"
#include "lib/allocate.h"
#include "model/common_methods.h"
#include "model/process.h"
#include "model/thread.h"
#include "test_macro.h"
#include "vm/vm.h"

#include <math.h>
#include <stdio.h>

static operation_result_t (*operations[])(process_t *, object_t *, object_t *) = {
    is_object_less_than,
    is_object_less_or_equal,
    is_object_greater_than,
    is_object_greater_or_equal,
    are_objects_equal,
    are_objects_not_equal};
static const opcode_t opcodes[] = {LESS, LEQ, GREATER, GREQ, EQUAL, DIFF};

typedef struct {
    int type; /* 0 integer, 1 real, 2 string, 3 boolean, 4 null, 5 function, 6 prototype */
    int64_t integer;
    double real;
    const wchar_t *string;
} value_tester_t;

static object_t *object_value(process_t *proc, value_tester_t value) {
    switch (value.type) {
        case 0:
            return create_integer_object(proc, value.integer);
        case 1:
            return create_real_number_object(proc, value.real);
        case 2:
            return create_string_object(
                proc,
                (string_value_t){value.string, wcslen(value.string), false});
        case 3:
            return get_boolean_object(value.integer);
        case 4:
            return get_null_object();
        case 5:
            return get_function_print();
        default:
            return get_function_proto();
    }
}

static const lattice_element_t *abstract_value(arena_t *arena, value_tester_t value) {
    switch (value.type) {
        case 0:
            return make_integer_constant_element(arena, value.integer);
        case 1:
            return make_real_constant_element(arena, value.real);
        case 2:
            return make_string_constant_element(
                arena,
                (string_view_t){value.string, wcslen(value.string)});
        case 3:
            return value.integer ? make_true_element() : make_false_element();
        case 4:
            return make_null_element();
        case 5:
            return make_function_element();
        default:
            return make_top_element();
    }
}

bool test_comparison_values(void) {
    const value_tester_t values[] = {{0, 0},
                                     {0, 1},
                                     {0, -1},
                                     {0, INT64_MAX},
                                     {0, INT64_MIN},
                                     {0, 9007199254740992LL},
                                     {0, 9007199254740993LL},
                                     {1, 0, 0.0},
                                     {1, 0, -0.0},
                                     {1, 0, 1.0},
                                     {1, 0, 0.5},
                                     {1, 0, -0.5},
                                     {1, 0, 0x1p53},
                                     {1, 0, 0x1p63},
                                     {1, 0, -0x1p63},
                                     {1, 0, INFINITY},
                                     {1, 0, -INFINITY},
                                     {1, 0, NAN},
                                     {2, 0, 0, L""},
                                     {2, 0, 0, L"abc"},
                                     {2, 0, 0, L"abd"},
                                     {2, 0, 0, L"\ue000"},
                                     {2, 0, 0, L"\U00010000"},
                                     {3, 0},
                                     {3, 1},
                                     {4},
                                     {5},
                                     {6}};

    struct {
        size_t a, b;
        comparison_order_t order;
    } cases[] = {{0, 1, ORDER_LESS},        {2, 0, ORDER_LESS},        {3, 4, ORDER_GREATER},
                 {5, 6, ORDER_LESS},        {6, 12, ORDER_GREATER},    {5, 12, ORDER_EQUAL},
                 {3, 13, ORDER_LESS},       {4, 14, ORDER_EQUAL},      {0, 7, ORDER_EQUAL},
                 {7, 8, ORDER_EQUAL},       {1, 9, ORDER_EQUAL},       {0, 10, ORDER_LESS},
                 {0, 11, ORDER_GREATER},    {4, 11, ORDER_LESS},       {3, 15, ORDER_LESS},
                 {4, 16, ORDER_GREATER},    {15, 15, ORDER_EQUAL},     {16, 16, ORDER_EQUAL},
                 {15, 16, ORDER_GREATER},   {17, 17, ORDER_UNORDERED}, {17, 0, ORDER_UNORDERED},
                 {17, 15, ORDER_UNORDERED}, {18, 19, ORDER_LESS},      {19, 19, ORDER_EQUAL},
                 {19, 20, ORDER_LESS},      {21, 22, ORDER_LESS},      {23, 24, ORDER_LESS},
                 {25, 25, ORDER_EQUAL},     {26, 26, ORDER_EQUAL},     {27, 27, ORDER_EQUAL},
                 {0, 23, ORDER_UNORDERED},  {0, 18, ORDER_UNORDERED},  {1, 24, ORDER_UNORDERED},
                 {25, 0, ORDER_UNORDERED},  {26, 19, ORDER_UNORDERED}, {27, 19, ORDER_UNORDERED}};

    for (size_t c = 0; c < sizeof(cases) / sizeof(*cases); c++)
        for (int reverse = 0; reverse < 2; reverse++)
            for (int op = 0; op < 6; op++) {
                value_tester_t av = values[reverse ? cases[c].b : cases[c].a],
                               bv = values[reverse ? cases[c].a : cases[c].b];
                comparison_order_t order = cases[c].order;
                if (reverse && order != ORDER_EQUAL && order != ORDER_UNORDERED)
                    order = order == ORDER_LESS ? ORDER_GREATER : ORDER_LESS;
                bool compatible =
                    (av.type <= 1 && bv.type <= 1) || (av.type == bv.type && av.type <= 3);
                bool exception = op < 4 && !compatible;
                bool expected = comparison_matches(order, (comparison_kind_t)op);
                process_t *proc = create_process();
                arena_t *arena = create_arena(8);
                object_t *a = object_value(proc, av), *b = object_value(proc, bv);
                operation_result_t r = operations[op](proc, a, b);
                ASSERT(r.is_exception == exception);
                if (exception) {
                    ASSERT(r.value
                           == (av.type >= 4 ? get_exception_invalid_operation()
                                            : get_exception_invalid_argument()));
                } else {
                    ASSERT(r.value == get_boolean_object(expected));
                }
                DECREF(r.value);
                const lattice_element_t *v = lattice_compare(abstract_value(arena, av),
                                                             abstract_value(arena, bv),
                                                             (comparison_kind_t)op);
                if (exception) {
                    ASSERT(v->type == LATTICE_BOTTOM || av.type == 6 || bv.type == 6);
                } else {
                    ASSERT(v->type == LATTICE_BOOLEAN
                           || v->type == (expected ? LATTICE_TRUE : LATTICE_FALSE));
                }
                code_builder_t *cb = create_code_builder();
                data_builder_t *db = create_data_builder();
                add_instruction(cb, (instruction_t){.opcode = opcodes[op]});
                add_instruction(cb, (instruction_t){.opcode = END});
                bytecode_t *code = link_code_and_data(cb, db);
                push_object_onto_stack(proc->main_thread->data_stack, a);
                push_object_onto_stack(proc->main_thread->data_stack, b);
                ASSERT((run(proc, code) != 0) == exception);
                if (exception) {
                    ASSERT(proc->main_thread->exception.value
                           == (av.type >= 4 ? get_exception_invalid_operation()
                                            : get_exception_invalid_argument()));
                } else {
                    ASSERT(peek_object_from_stack(proc->main_thread->data_stack, 0)
                           == get_boolean_object(expected));
                }
                destroy_code_builder(cb);
                destroy_data_builder(db);
                free_bytecode(code);
                destroy_process(proc);
                destroy_arena(arena);
            }
    return true;
}

bool test_comparison_ranges(void) {
    arena_t *arena = create_arena(8);
    for (int a = -3; a <= 3; a++)
        for (int b = a; b <= 3; b++)
            for (int c = -3; c <= 3; c++)
                for (int d = c; d <= 3; d++)
                    for (int op = 0; op < 6; op++) {
                        bool yes = false, no = false;
                        for (int x = a; x <= b; x++)
                            for (int y = c; y <= d; y++) {
                                bool r = comparison_matches(x < y   ? ORDER_LESS
                                                            : x > y ? ORDER_GREATER
                                                                    : ORDER_EQUAL,
                                                            (comparison_kind_t)op);
                                yes |= r;
                                no |= !r;
                            }
                        const lattice_element_t *v =
                            lattice_compare(make_integer_range_element(arena, a, b),
                                            make_integer_range_element(arena, c, d),
                                            (comparison_kind_t)op);
                        ASSERT(v->type
                               == (yes && no ? LATTICE_BOOLEAN
                                   : yes     ? LATTICE_TRUE
                                             : LATTICE_FALSE));
                    }
    for (int op = 0; op < 6; op++) {
        ASSERT(lattice_compare(make_bottom_element(), make_top_element(), op)->type
               == LATTICE_BOTTOM);
        ASSERT(lattice_compare(make_real_element(), make_real_element(), op)->type
               == LATTICE_BOOLEAN);
        ASSERT(lattice_compare(make_top_element(), make_top_element(), op)->type
               == LATTICE_BOOLEAN);
        const lattice_element_t *v =
            lattice_compare(make_real_constant_element(arena, NAN), make_numeric_element(), op);
        ASSERT(v->type == (op == COMPARE_NOT_EQUAL ? LATTICE_TRUE : LATTICE_FALSE));
    }
    ASSERT(lattice_compare(make_integer_element(),
                           make_real_constant_element(arena, 0x1p63),
                           COMPARE_LESS)
               ->type
           == LATTICE_TRUE);
    ASSERT(lattice_compare(make_integer_element(),
                           make_real_constant_element(arena, -0x1p63),
                           COMPARE_GREQ)
               ->type
           == LATTICE_TRUE);
    ASSERT(lattice_compare(make_null_element(), make_not_null_element(), COMPARE_EQUAL)->type
           == LATTICE_FALSE);
    ASSERT(lattice_compare(make_function_element(), make_function_element(), COMPARE_EQUAL)->type
           == LATTICE_BOOLEAN);
    destroy_arena(arena);
    return true;
}

bool test_comparison_nodes(void) {
    expression_t *(*factories[])(arena_t *, expression_t *, expression_t *) = {
        create_less_node,
        create_less_or_equal_node,
        create_greater_node,
        create_greater_or_equal_node,
        create_equal_node,
        create_not_equal_node};
    for (int op = 0; op < 6; op++) {
        arena_t *arena = create_arena(8);
        abstract_state_t *state = create_abstract_state(arena);
        expression_t *left = (expression_t *)create_integer_node(arena, 1),
                     *right = (expression_t *)create_integer_node(arena, 2);
        expression_t *expr = factories[op](arena, left, right);
        ASSERT(get_node_child_count(&expr->base) == 2
               && get_node_child(&expr->base, 0) == &left->base
               && get_node_child(&expr->base, 1) == &right->base);
        bool expected = comparison_matches(ORDER_LESS, op);
        ASSERT(calculate_expression(expr, state, arena)->type
               == (expected ? LATTICE_TRUE : LATTICE_FALSE));
        code_builder_t *cb = create_code_builder();
        data_builder_t *db = create_data_builder();
        generate_bytecode_from_expression(expr, cb, db);
        ASSERT(cb->size == 3 && cb->instructions[2].opcode == opcodes[op]);
        string_value_t text = generate_goat_code_from_expression(expr);
        parser_memory_t memory = {arena, arena, arena, arena};
        ASSERT(parse_analysis_test_program(&memory, text));
        source_builder_t *sb = create_source_builder();
        generate_indented_goat_code_from_expression(expr, sb, 0);
        string_value_t pretty = build_source(sb);
        ASSERT(pretty.length == text.length + 1 && !wmemcmp(pretty.data, text.data, text.length));
        FREE_STRING(text);
        FREE_STRING(pretty);
        destroy_source_builder(sb);
        destroy_code_builder(cb);
        destroy_data_builder(db);
        destroy_abstract_state(state);
        destroy_arena(arena);
    }
    const wchar_t *invalid[] = {L"1 <=", L">= 2", L"1 ==", L"!= 2", L"1 === 2", L"1 <> 2"};
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

bool test_comparison_keys(void) {
    process_t *proc = create_process();
    object_t *a = create_integer_object(proc, 9007199254740992LL),
             *b = create_integer_object(proc, 9007199254740993LL);
    object_t *nan = create_real_number_object(proc, NAN),
             *inf = create_real_number_object(proc, INFINITY);
    ASSERT(compare_objects_using_vtbl(a, b) < 0 && compare_objects_using_vtbl(b, a) > 0);
    ASSERT(compare_objects_using_vtbl(nan, inf) > 0 && compare_objects_using_vtbl(inf, nan) < 0);
    ASSERT(compare_objects_using_vtbl(nan, nan) == 0);
    const wchar_t first[] = {L'a', 0, L'b', 0}, second[] = {L'a', 0, L'c', 0};
    object_t *s = create_string_object(proc, (string_value_t){first, 3, false}),
             *t = create_string_object(proc, (string_value_t){second, 3, false});
    ASSERT(compare_objects_using_vtbl(s, t) < 0);
    operation_result_t r = is_object_less_than(proc, s, t);
    ASSERT(!r.is_exception && r.value == get_boolean_object(true));
    DECREF(r.value);
    object_t *root = get_root_object();
    object_t *map = create_user_defined_object(proc, (object_array_t){&root, 1});
    object_t *keys[] = {a, b, nan, inf, s, t};
    for (size_t i = 0; i < 6; i++)
        ASSERT(create_object_property(map, keys[i], get_static_integer_object((int64_t)i), false)
               == MSTAT_OK);
    for (size_t i = 0; i < 6; i++)
        ASSERT(get_object_property(map, keys[i]) == get_static_integer_object((int64_t)i));
    DECREF(map);
    DECREF(a);
    DECREF(b);
    DECREF(nan);
    DECREF(inf);
    DECREF(s);
    DECREF(t);
    destroy_process(proc);
    return true;
}
