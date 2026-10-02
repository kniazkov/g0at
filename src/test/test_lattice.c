/**
 * @file test_lattice.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Lattice examples, boundaries, and algebraic laws.
 */
#include "test_lattice.h"

#include "analysis/lattice.h"
#include "graph/node.h"
#include "model/builtin_function.h"
#include "test_macro.h"

#include <math.h>
#include <stdio.h>

/** @brief Compares abstract values without using join, meet, or display strings. */
static bool equal(const lattice_element_t *a, const lattice_element_t *b) {
    if (a->type != b->type) {
        return false;
    }
    switch (a->type) {
        case LATTICE_INTEGER_CONSTANT:
            return ((const integer_constant_element_t *)a)->value
                   == ((const integer_constant_element_t *)b)->value;
        case LATTICE_INTEGER_RANGE: {
            const integer_range_element_t *x = (const integer_range_element_t *)a;
            const integer_range_element_t *y = (const integer_range_element_t *)b;
            return x->min == y->min && x->max == y->max;
        }
        case LATTICE_REAL_CONSTANT: {
            double x = ((const real_constant_element_t *)a)->value;
            double y = ((const real_constant_element_t *)b)->value;
            if (isnan(x)) {
                return isnan(y);
            }
            if (x == 0.0 && y == 0.0) {
                return !!signbit(x) == !!signbit(y);
            }
            return x == y;
        }
        case LATTICE_STRING_CONSTANT: {
            string_view_t x = ((const string_constant_element_t *)a)->value;
            string_view_t y = ((const string_constant_element_t *)b)->value;
            return x.length == y.length && (!x.length || !wmemcmp(x.data, y.data, x.length));
        }
        case LATTICE_KNOWN_FUNCTION: {
            const known_function_element_t *x = (const known_function_element_t *)a;
            const known_function_element_t *y = (const known_function_element_t *)b;
            return x->node == y->node && x->owner == y->owner && x->builtin == y->builtin;
        }
        case LATTICE_TYPED_ARRAY:
            return ((const typed_array_element_t *)a)->element_type
                   == ((const typed_array_element_t *)b)->element_type;
        default:
            return true;
    }
}

bool test_lattice_examples() {
    arena_t *arena = create_arena(8);
    const lattice_element_t *one = make_integer_constant_element(arena, 1);
    const lattice_element_t *three = make_integer_constant_element(arena, 3);
    const lattice_element_t *r13 = make_integer_range_element(arena, 1, 3);
    const lattice_element_t *r35 = make_integer_range_element(arena, 3, 5);
    const lattice_element_t *real = make_real_constant_element(arena, 1.0);
    const lattice_element_t *str = make_string_constant_element(arena, (string_view_t){L"a", 1});
    const lattice_element_t *str_copy =
        make_string_constant_element(arena, (string_view_t){L"a!", 1});
    const lattice_element_t *other = make_string_constant_element(arena, (string_view_t){L"b", 1});

    struct {
        const lattice_element_t *left, *right, *join, *meet;
    } cases[] = {
        {one, three, r13, make_bottom_element()},
        {r13, r35, make_integer_range_element(arena, 1, 5), three},
        {r13, one, r13, one},
        {r13,
         make_integer_range_element(arena, 5, 8),
         make_integer_range_element(arena, 1, 8),
         make_bottom_element()},
        {one, real, make_numeric_element(), make_bottom_element()},
        {make_integer_element(),
         make_real_element(),
         make_numeric_element(),
         make_bottom_element()},
        {make_numeric_element(), one, make_numeric_element(), one},
        {make_true_element(), make_false_element(), make_boolean_element(), make_bottom_element()},
        {make_boolean_element(), make_true_element(), make_boolean_element(), make_true_element()},
        {str, str_copy, str, str},
        {str, other, make_string_element(), make_bottom_element()},
        {str, make_string_element(), make_string_element(), str},
        {one, str, make_not_null_element(), make_bottom_element()},
        {one, make_null_element(), make_top_element(), make_bottom_element()},
        {make_null_element(), make_not_null_element(), make_top_element(), make_bottom_element()}};

    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        ASSERT(equal(lattice_join(arena, cases[i].left, cases[i].right), cases[i].join));
        ASSERT(equal(lattice_join(arena, cases[i].right, cases[i].left), cases[i].join));
        ASSERT(equal(lattice_meet(arena, cases[i].left, cases[i].right), cases[i].meet));
        ASSERT(equal(lattice_meet(arena, cases[i].right, cases[i].left), cases[i].meet));
    }
    destroy_arena(arena);
    return true;
}

bool test_lattice_boundaries() {
    arena_t *arena = create_arena(8);
    ASSERT(make_integer_range_element(arena, 5, 1)->type == LATTICE_BOTTOM);
    const lattice_element_t *single = make_integer_range_element(arena, 7, 7);
    ASSERT(single->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(((const integer_constant_element_t *)single)->value == 7);
    ASSERT(make_integer_range_element(arena, INT64_MIN, INT64_MAX)->type == LATTICE_INTEGER);
    const lattice_element_t *low = make_integer_constant_element(arena, INT64_MIN);
    const lattice_element_t *high = make_integer_constant_element(arena, INT64_MAX);
    ASSERT(lattice_join(arena, low, high)->type == LATTICE_INTEGER);
    ASSERT(equal(lattice_meet(arena,
                              make_integer_range_element(arena, INT64_MIN, 0),
                              make_integer_range_element(arena, 0, INT64_MAX)),
                 make_integer_constant_element(arena, 0)));
    ASSERT(equal(lattice_join(arena, make_integer_range_element(arena, 5, 1), high), high));
    ASSERT(equal(make_integer_range_element(arena, INT64_MIN, INT64_MIN), low));
    ASSERT(equal(make_integer_range_element(arena, INT64_MAX, INT64_MAX), high));

    const lattice_element_t *nan1 = make_real_constant_element(arena, NAN);
    const lattice_element_t *nan2 = make_real_constant_element(arena, -NAN);
    ASSERT(lattice_join(arena, nan1, nan1)->type == LATTICE_REAL_CONSTANT);
    ASSERT(lattice_meet(arena, nan1, nan1)->type == LATTICE_REAL_CONSTANT);
    ASSERT(equal(lattice_join(arena, nan1, nan2), nan1));
    ASSERT(equal(lattice_meet(arena, nan1, nan2), nan1));
    const lattice_element_t *pos = make_real_constant_element(arena, 0.0);
    const lattice_element_t *neg = make_real_constant_element(arena, -0.0);
    ASSERT(lattice_join(arena, pos, neg)->type == LATTICE_REAL);
    ASSERT(lattice_meet(arena, pos, neg)->type == LATTICE_BOTTOM);
    ASSERT(equal(lattice_join(arena, neg, neg), neg));
    ASSERT(equal(lattice_meet(arena, pos, pos), pos));
    const lattice_element_t *inf = make_real_constant_element(arena, INFINITY);
    const lattice_element_t *minus_inf = make_real_constant_element(arena, -INFINITY);
    ASSERT(equal(lattice_join(arena, inf, inf), inf));
    ASSERT(equal(lattice_meet(arena, inf, inf), inf));
    ASSERT(lattice_join(arena, inf, minus_inf)->type == LATTICE_REAL);
    ASSERT(lattice_meet(arena, inf, minus_inf)->type == LATTICE_BOTTOM);
    ASSERT(lattice_join(arena, nan1, inf)->type == LATTICE_REAL);
    ASSERT(lattice_meet(arena, nan1, inf)->type == LATTICE_BOTTOM);
    destroy_arena(arena);
    return true;
}

bool test_lattice_arrays() {
    arena_t *arena = create_arena(8);

    struct {
        lattice_type_t left, right, join, meet;
    } cases[] = {{LATTICE_INTEGER, LATTICE_NUMERIC, LATTICE_NUMERIC, LATTICE_INTEGER},
                 {LATTICE_INTEGER, LATTICE_REAL, LATTICE_NUMERIC, LATTICE_BOTTOM},
                 {LATTICE_INTEGER, LATTICE_STRING, LATTICE_NOT_NULL, LATTICE_BOTTOM},
                 {LATTICE_TRUE, LATTICE_FALSE, LATTICE_BOOLEAN, LATTICE_BOTTOM},
                 {LATTICE_TRUE, LATTICE_BOOLEAN, LATTICE_BOOLEAN, LATTICE_TRUE},
                 {LATTICE_NULL, LATTICE_INTEGER, LATTICE_TOP, LATTICE_BOTTOM},
                 {LATTICE_NULL, LATTICE_NOT_NULL, LATTICE_TOP, LATTICE_BOTTOM},
                 {LATTICE_BOTTOM, LATTICE_INTEGER, LATTICE_INTEGER, LATTICE_BOTTOM},
                 {LATTICE_TOP, LATTICE_INTEGER, LATTICE_TOP, LATTICE_INTEGER},
                 {LATTICE_ARRAY, LATTICE_NOT_NULL, LATTICE_NOT_NULL, LATTICE_ARRAY},
                 {LATTICE_FUNCTION, LATTICE_FUNCTION, LATTICE_FUNCTION, LATTICE_FUNCTION}};

    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        const lattice_element_t *left = make_typed_array_element(arena, cases[i].left);
        const lattice_element_t *right = make_typed_array_element(arena, cases[i].right);
        const lattice_element_t *join = make_typed_array_element(arena, cases[i].join);
        const lattice_element_t *meet = make_typed_array_element(arena, cases[i].meet);
        ASSERT(equal(lattice_join(arena, left, right), join));
        ASSERT(equal(lattice_join(arena, right, left), join));
        ASSERT(equal(lattice_meet(arena, left, right), meet));
        ASSERT(equal(lattice_meet(arena, right, left), meet));
    }
    const lattice_element_t *empty = make_typed_array_element(arena, LATTICE_BOTTOM);
    ASSERT(empty->type == LATTICE_TYPED_ARRAY);
    ASSERT(lattice_meet(arena, empty, make_null_element())->type == LATTICE_BOTTOM);
    ASSERT(make_typed_array_element(arena, LATTICE_TOP)->type == LATTICE_ARRAY);
    destroy_arena(arena);
    return true;
}

static bool law(bool holds, const char *name, size_t i, size_t j, size_t k) {
    if (!holds) {
        printf("Lattice law '%s' failed for sample indices %zu, %zu, %zu\n", name, i, j, k);
    }
    return holds;
}

bool test_lattice_laws() {
    arena_t *values = create_arena(8);
    const lattice_element_t *samples[64];
    size_t count = 0;
    const lattice_element_t *generic[] = {make_top_element(),
                                          make_not_null_element(),
                                          make_null_element(),
                                          make_numeric_element(),
                                          make_integer_element(),
                                          make_real_element(),
                                          make_string_element(),
                                          make_boolean_element(),
                                          make_true_element(),
                                          make_false_element(),
                                          make_function_element(),
                                          make_array_element(),
                                          make_user_defined_object_element(),
                                          make_bottom_element()};
    for (size_t i = 0; i < sizeof(generic) / sizeof(*generic); i++) {
        samples[count++] = generic[i];
        samples[count++] = make_typed_array_element(values, generic[i]->type);
    }
    node_t function_a = {0}, function_b = {0};
    samples[count++] = make_known_function_element(values, &function_a, NULL);
    samples[count++] = make_known_function_element(values, &function_a, NULL);
    samples[count++] = make_known_function_element(values, &function_b, NULL);
    size_t builtin_count;
    const builtin_function_t *const *builtins = get_builtin_functions(&builtin_count);
    for (size_t i = 0; i < builtin_count; i++)
        samples[count++] = make_builtin_function_element(values, builtins[i]);
    int64_t integers[] = {INT64_MIN, -1, 0, 1, INT64_MAX};
    for (size_t i = 0; i < sizeof(integers) / sizeof(*integers); i++) {
        samples[count++] = make_integer_constant_element(values, integers[i]);
    }
    samples[count++] = make_integer_range_element(values, INT64_MIN, 0);
    samples[count++] = make_integer_range_element(values, -2, 1);
    samples[count++] = make_integer_range_element(values, 0, INT64_MAX);
    samples[count++] = make_integer_range_element(values, 2, 3);
    double reals[] = {-INFINITY, -1.5, -0.0, 0.0, 1.5, INFINITY, NAN, -NAN};
    for (size_t i = 0; i < sizeof(reals) / sizeof(*reals); i++) {
        samples[count++] = make_real_constant_element(values, reals[i]);
    }
    samples[count++] = make_string_constant_element(values, (string_view_t){L"", 0});
    samples[count++] = make_string_constant_element(values, (string_view_t){L"a", 1});
    samples[count++] = make_string_constant_element(values, (string_view_t){L"b", 1});
    samples[count++] = make_string_constant_element(values, (string_view_t){L"a\0b", 3});
    for (size_t i = 0; i < count; i++) {
        arena_t *arena = create_arena(8);
        const lattice_element_t *a = samples[i];
        ASSERT(law(equal(lattice_join(arena, a, a), a), "join idempotence", i, i, i));
        ASSERT(law(equal(lattice_meet(arena, a, a), a), "meet idempotence", i, i, i));
        ASSERT(equal(lattice_join(arena, a, make_bottom_element()), a));
        ASSERT(equal(lattice_meet(arena, a, make_top_element()), a));
        ASSERT(lattice_join(arena, a, make_top_element())->type == LATTICE_TOP);
        ASSERT(lattice_meet(arena, a, make_bottom_element())->type == LATTICE_BOTTOM);
        for (size_t j = 0; j < count; j++) {
            const lattice_element_t *b = samples[j];
            const lattice_element_t *join = lattice_join(arena, a, b);
            const lattice_element_t *meet = lattice_meet(arena, a, b);
            ASSERT(law(equal(join, lattice_join(arena, b, a)), "join commutativity", i, j, 0));
            ASSERT(law(equal(meet, lattice_meet(arena, b, a)), "meet commutativity", i, j, 0));
            ASSERT(law(equal(lattice_join(arena, a, meet), a), "join absorption", i, j, 0));
            ASSERT(law(equal(lattice_meet(arena, a, join), a), "meet absorption", i, j, 0));
            for (size_t k = 0; k < count; k++) {
                const lattice_element_t *c = samples[k];
                ASSERT(law(equal(lattice_join(arena, join, c),
                                 lattice_join(arena, a, lattice_join(arena, b, c))),
                           "join associativity",
                           i,
                           j,
                           k));
                ASSERT(law(equal(lattice_meet(arena, meet, c),
                                 lattice_meet(arena, a, lattice_meet(arena, b, c))),
                           "meet associativity",
                           i,
                           j,
                           k));
            }
        }
        destroy_arena(arena);
    }
    destroy_arena(values);
    return true;
}
