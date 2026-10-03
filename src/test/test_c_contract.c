/** @file test_c_contract.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Numeric representation and C precondition tests.
 */
#include "test_c_contract.h"

#include "analysis/c_contract.h"
#include "graph/expression.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <math.h>
#include <stdio.h>
#include <wchar.h>

bool test_c_contract_types() {
    const struct {
        lattice_type_t type;
        c_value_type_t expected;
    } cases[] = {{LATTICE_TOP, C_VALUE_UNKNOWN},
                 {LATTICE_NOT_NULL, C_VALUE_UNKNOWN},
                 {LATTICE_NUMERIC, C_VALUE_UNKNOWN},
                 {LATTICE_BOTTOM, C_VALUE_UNKNOWN},
                 {LATTICE_INTEGER, C_VALUE_INT64},
                 {LATTICE_INTEGER_CONSTANT, C_VALUE_INT64},
                 {LATTICE_INTEGER_RANGE, C_VALUE_INT64},
                 {LATTICE_REAL, C_VALUE_DOUBLE},
                 {LATTICE_REAL_CONSTANT, C_VALUE_DOUBLE},
                 {LATTICE_NULL, C_VALUE_UNSUPPORTED},
                 {LATTICE_BOOLEAN, C_VALUE_UNSUPPORTED},
                 {LATTICE_TRUE, C_VALUE_UNSUPPORTED},
                 {LATTICE_FALSE, C_VALUE_UNSUPPORTED},
                 {LATTICE_STRING, C_VALUE_UNSUPPORTED},
                 {LATTICE_STRING_CONSTANT, C_VALUE_UNSUPPORTED},
                 {LATTICE_FUNCTION, C_VALUE_UNSUPPORTED},
                 {LATTICE_KNOWN_FUNCTION, C_VALUE_UNSUPPORTED},
                 {LATTICE_ARRAY, C_VALUE_UNSUPPORTED},
                 {LATTICE_TYPED_ARRAY, C_VALUE_UNSUPPORTED},
                 {LATTICE_USER_DEFINED_OBJECT, C_VALUE_UNSUPPORTED}};

    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        ASSERT(classify_c_value_type(cases[i].type) == cases[i].expected);
    }
    return true;
}

bool test_c_contract_checks() {
    arena_t *arena = create_arena(16);
    node_t *function = create_function_object_node(arena, NULL, 0);
    function_summary_t *summary = create_function_summary(arena, function, 1);
    ASSERT(summary->c_blockers == C_BLOCKER_ANALYSIS);
    summary->status = FUNCTION_ANALYZED;
    summary->effects = summary->direct_effects = FUNCTION_EFFECT_NONE;
    summary->parameter_types[0] = make_integer_element();
    summary->return_type = make_real_element();
    uint32_t flags = function->flags;
    summary->c_support = FUNCTION_C_SUPPORTED;
    check_function_c_contract(summary);
    ASSERT(summary->c_support == FUNCTION_C_UNKNOWN && summary->c_blockers == C_BLOCKER_BODY);
    ASSERT(function->flags == flags);
    const function_summary_t *snapshot = snapshot_function_summary(arena, summary);
    summary->parameter_types[0] = make_null_element();
    check_function_c_contract(summary);
    ASSERT(summary->c_support == FUNCTION_C_UNSUPPORTED);
    ASSERT(summary->c_blockers == (C_BLOCKER_PARAMETERS | C_BLOCKER_BODY));
    summary->parameter_types[0] = make_numeric_element();
    summary->return_type = make_top_element();
    summary->effects = FUNCTION_EFFECT_UNKNOWN;
    check_function_c_contract(summary);
    ASSERT(summary->c_support == FUNCTION_C_UNKNOWN);
    ASSERT(summary->c_blockers
           == (C_BLOCKER_PARAMETERS | C_BLOCKER_RETURN | C_BLOCKER_EFFECTS | C_BLOCKER_BODY));
    summary->effects = FUNCTION_EFFECT_NONE;
    summary->parameter_types[0] = make_integer_range_element(arena, INT64_MIN, INT64_MAX);
    const double values[] = {NAN, INFINITY, -INFINITY, -0.0, 0.0, 1.0};
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); i++) {
        summary->return_type = make_real_constant_element(arena, values[i]);
        check_function_c_contract(summary);
        ASSERT(summary->c_support == FUNCTION_C_UNKNOWN && summary->c_blockers == C_BLOCKER_BODY);
    }
    summary->return_type = make_bottom_element();
    check_function_c_contract(summary);
    ASSERT(summary->c_blockers == (C_BLOCKER_RETURN | C_BLOCKER_BODY));
    reset_function_summary(summary);
    ASSERT(summary->c_support == FUNCTION_C_UNKNOWN && summary->c_blockers == C_BLOCKER_ANALYSIS);
    ASSERT(snapshot->c_blockers == C_BLOCKER_BODY && snapshot->c_support == FUNCTION_C_UNKNOWN);
    string_value_t text = function_summary_to_string(snapshot);
    ASSERT(wcsstr(text.data, L"c-blockers=body"));
    FREE_STRING(text);
    destroy_arena(arena);
    return true;
}
