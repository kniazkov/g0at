/**
 * @file test_if.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Truthiness, branch dispatch, and returned-path merging.
 */
#include <stdio.h>
#include <math.h>

#include "test_macro.h"
#include "analysis_test_support.h"
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "graph/expression.h"
#include "graph/statement.h"
#include "graph/declarations.h"
#include "model/object.h"
#include "model/process.h"
#include "cli/options.h"
#include "analysis/analysis.h"

bool test_abstract_truthiness() {
    arena_t *arena = create_arena(8);
    struct { const lattice_element_t *value; abstract_truth_t truth; } cases[] = {
        { make_bottom_element(), ABSTRACT_NEVER }, { make_top_element(), ABSTRACT_EITHER },
        { make_not_null_element(), ABSTRACT_EITHER }, { make_null_element(), ABSTRACT_FALSE },
        { make_true_element(), ABSTRACT_TRUE }, { make_false_element(), ABSTRACT_FALSE },
        { make_boolean_element(), ABSTRACT_EITHER }, { make_numeric_element(), ABSTRACT_EITHER },
        { make_integer_element(), ABSTRACT_EITHER }, { make_real_element(), ABSTRACT_EITHER },
        { make_string_element(), ABSTRACT_EITHER }, { make_function_element(), ABSTRACT_TRUE },
        { make_user_defined_object_element(), ABSTRACT_EITHER }, { make_array_element(), ABSTRACT_EITHER },
        { make_integer_constant_element(arena, 0), ABSTRACT_FALSE },
        { make_integer_constant_element(arena, INT64_MIN), ABSTRACT_TRUE },
        { make_integer_constant_element(arena, INT64_MAX), ABSTRACT_TRUE },
        { make_integer_range_element(arena, -5, -1), ABSTRACT_TRUE },
        { make_integer_range_element(arena, 1, 5), ABSTRACT_TRUE },
        { make_integer_range_element(arena, -5, 0), ABSTRACT_EITHER },
        { make_integer_range_element(arena, 0, 5), ABSTRACT_EITHER },
        { make_integer_range_element(arena, -5, 5), ABSTRACT_EITHER },
        { make_string_constant_element(arena, (string_view_t){ L"", 0 }), ABSTRACT_FALSE },
        { make_string_constant_element(arena, (string_view_t){ L"\0", 1 }), ABSTRACT_TRUE }
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        ASSERT(lattice_truth(cases[i].value) == cases[i].truth);
    }
    process_t *process = create_process();
    double reals[] = { 0.0, -0.0, 0.5, -0.5, 1.0, INFINITY, -INFINITY, NAN };
    for (size_t i = 0; i < sizeof(reals) / sizeof(*reals); i++) {
        bool expected = reals[i] != 0.0;
        const lattice_element_t *value = make_real_constant_element(arena, reals[i]);
        ASSERT(lattice_truth(value) == (expected ? ABSTRACT_TRUE : ABSTRACT_FALSE));
        object_t *object = create_real_number_object(process, reals[i]);
        ASSERT(get_object_boolean_value(object) == expected);
    }
    destroy_process(process);
    destroy_arena(arena);
    return true;
}

typedef struct {
    expression_t base;
    const lattice_element_t *value;
    int visits;
} condition_probe_t;

typedef struct {
    statement_t base;
    declarator_t *declarator;
    int64_t value;
    control_flow_t flow;
    int visits;
} branch_probe_t;

static const lattice_element_t *condition_value(node_t *node, abstract_state_t *state, arena_t *arena) {
    condition_probe_t *probe = (condition_probe_t *)node;
    probe->visits++;
    return probe->value;
}

static abstract_state_t *branch_value(node_t *node, abstract_state_t *state, arena_t *arena) {
    branch_probe_t *probe = (branch_probe_t *)node;
    probe->visits++;
    set_in_abstract_state(state, probe->declarator, make_integer_constant_element(arena, probe->value));
    state->control_flow = probe->flow;
    return state;
}

bool test_if_dispatch() {
    node_vtbl_t condition_vtbl = { .calculate = condition_value };
    node_vtbl_t branch_vtbl = { .execute = branch_value };
    const lattice_element_t *conditions[] = {
        make_true_element(), make_false_element(), make_top_element(), make_bottom_element()
    };
    for (size_t c = 0; c < 4; c++) {
        for (int has_else = 0; has_else <= 1; has_else++) {
            for (int returned = 0; returned <= 1; returned++) {
                arena_t *arena = create_arena(8);
                abstract_state_t *state = create_abstract_state(arena);
                declarator_t decl = {0};
                set_in_abstract_state(state, &decl, make_integer_constant_element(arena, 0));
                condition_probe_t condition = { .base.base.vtbl = &condition_vtbl, .value = conditions[c] };
                branch_probe_t yes = { .base.base.vtbl = &branch_vtbl, .declarator = &decl,
                    .value = 1, .flow = returned ? FLOW_RETURN : FLOW_NORMAL };
                branch_probe_t no = { .base.base.vtbl = &branch_vtbl, .declarator = &decl,
                    .value = 2, .flow = FLOW_NORMAL };
                node_t *node = create_if_else_node(arena, &condition.base, &yes.base,
                    has_else ? &no.base : NULL);
                ASSERT(execute_node(node, state, arena) == state);
                ASSERT(condition.visits == 1);
                ASSERT(yes.visits == (c == 0 || c == 2));
                ASSERT(no.visits == (has_else && (c == 1 || c == 2)));
                ASSERT(state->control_flow == (c == 3 ? FLOW_UNREACHABLE :
                    c == 0 && returned ? FLOW_RETURN : FLOW_NORMAL));
                if (c == 2 && returned) {
                    const lattice_element_t *value = get_from_abstract_state(state, &decl);
                    ASSERT(value->type == LATTICE_INTEGER_CONSTANT);
                    ASSERT(((const integer_constant_element_t *)value)->value == (has_else ? 2 : 0));
                }
                if (state->control_flow != FLOW_NORMAL) {
                    execute_node(node, state, arena);
                    ASSERT(condition.visits == 1);
                }
                destroy_abstract_state(state);
                destroy_arena(arena);
            }
        }
    }
    return true;
}

bool test_if_return_values() {
    arena_t *arena = create_arena(8);
    parser_memory_t memory = { arena, arena, arena, arena };
    node_t *root = parse_analysis_test_program(&memory, STATIC_STRING(
        L"var x = 0; if (1 < 2) { return (x = 1); } else { return (x = 3); } x = 99;"));
    ASSERT(root);
    options_t *options = create_options();
    ASSERT(!analyze(root, &memory, options, NULL));
    abstract_state_t *state = create_abstract_state(arena);
    const lattice_element_t *returned = make_bottom_element();
    state->return_value = &returned;
    ASSERT(execute_node(root, state, arena) == state);
    ASSERT(state->control_flow == FLOW_RETURN);
    ASSERT(returned->type == LATTICE_INTEGER_RANGE);
    ASSERT(((const integer_range_element_t *)returned)->min == 1);
    ASSERT(((const integer_range_element_t *)returned)->max == 3);
    destroy_abstract_state(state);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
