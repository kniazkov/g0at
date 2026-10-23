/** @file conditional.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Lazy conditional values and branch-state joins.
 */
#include "analysis/reachability.h"
#include "analysis/simplification.h"
#include "codegen/c_control.h"
#include "codegen/c_lowering.h"
#include "codegen/code_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "expression.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

typedef struct {
    expression_t base;
    expression_t *parts[3];
} conditional_t;

static size_t count(const node_t *node) {
    return 3;
}

static node_t *child(const node_t *node, size_t index) {
    return index < 3 ? (node_t *)((const conditional_t *)node)->parts[index] : NULL;
}

static const gchar_t *tag(const node_t *node, size_t index) {
    static const gchar_t *tags[] = {U"condition", U"true", U"false"};
    return index < 3 ? tags[index] : NULL;
}

static bool replace(node_t *node, node_t *old, node_t *replacement) {
    if (!is_expression(replacement->vtbl->type))
        return false;
    for (size_t i = 0; i < 3; i++) {
        if (child(node, i) == old) {
            ((conditional_t *)node)->parts[i] = (expression_t *)replacement;
            return true;
        }
    }
    return false;
}

/** @brief Uses the same lazy semantics for interpretation and immediate reachability. */
static const lattice_element_t *visit(node_t *node,
                                      abstract_state_t **state,
                                      arena_t *arena,
                                      analysis_collector_t *collector,
                                      bool reachable) {
    return reachable ? visit_reachable_node(node, state, collector)
                     : calculate_expression((expression_t *)node, *state, arena);
}

static const lattice_element_t *evaluate(node_t *node,
                                         abstract_state_t **state,
                                         arena_t *arena,
                                         analysis_collector_t *collector,
                                         bool reachable) {
    const lattice_element_t *condition = visit(child(node, 0), state, arena, collector, reachable);
    abstract_truth_t truth = lattice_truth(condition);
    if ((*state)->control_flow != FLOW_NORMAL || truth == ABSTRACT_NEVER) {
        if (reachable) {
            mark_unreachable_subtree(child(node, 1), collector);
            mark_unreachable_subtree(child(node, 2), collector);
        }
        return make_bottom_element();
    }
    if (truth != ABSTRACT_EITHER) {
        size_t selected = truth == ABSTRACT_TRUE ? 1 : 2;
        if (reachable)
            mark_unreachable_subtree(child(node, 3 - selected), collector);
        return visit(child(node, selected), state, arena, collector, reachable);
    }
    abstract_state_t *left = clone_abstract_state(*state);
    abstract_state_t *right = clone_abstract_state(*state);
    const lattice_element_t *a = visit(child(node, 1), &left, arena, collector, reachable);
    const lattice_element_t *b = visit(child(node, 2), &right, arena, collector, reachable);
    if (left->control_flow != FLOW_NORMAL)
        a = make_bottom_element();
    if (right->control_flow != FLOW_NORMAL)
        b = make_bottom_element();
    abstract_state_t *merged = join_abstract_states(left, right);
    /* Expression callers retain the original state address. */
    abstract_state_t previous = **state;
    **state = *merged;
    *merged = previous;
    destroy_abstract_state(merged);
    destroy_abstract_state(left);
    destroy_abstract_state(right);
    if ((*state)->control_flow == FLOW_NORMAL)
        collect_joined_abstract_state(*state, node);
    return lattice_join(arena, a, b);
}

static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    return evaluate(node, &state, arena, NULL, false);
}

static const lattice_element_t *
reach(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    return evaluate(node, state, (*state)->arena, collector, true);
}

/** @brief Explicit grouping preserves nesting and assignment operands when regenerated. */
static string_value_t source(const node_t *node) {
    string_value_t a = generate_goat_code_from_node(child(node, 0));
    string_value_t b = generate_goat_code_from_node(child(node, 1));
    string_value_t c = generate_goat_code_from_node(child(node, 2));
    string_value_t result = format_string(U"(%s ? %s : %s)", a.data, b.data, c.data);
    FREE_STRING(a);
    FREE_STRING(b);
    FREE_STRING(c);
    return result;
}

static void indented_source(const node_t *node, source_builder_t *builder, size_t indent) {
    append_formatted_source(builder, source(node));
}

static instr_index_t bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    instr_index_t first = generate_bytecode_from_node(child(node, 0), code, data);
    instr_index_t otherwise = add_instruction(code, (instruction_t){.opcode = JIF});
    generate_bytecode_from_node(child(node, 1), code, data);
    instr_index_t end = add_instruction(code, (instruction_t){.opcode = JUMP});
    get_instruction(code, otherwise)->arg1 = get_next_instruction_index(code);
    generate_bytecode_from_node(child(node, 2), code, data);
    get_instruction(code, end)->arg1 = get_next_instruction_index(code);
    return first;
}

/** @brief Preserve exact result categories; heterogeneous branches stay in the VM. */
static bool
native(const node_t *node, const lattice_element_t *value, const c_expression_context_t *context) {
    const lattice_element_t *known = c_expression_constant(context, child(node, 0));
    abstract_truth_t truth = known ? lattice_truth(known) : ABSTRACT_EITHER;
    if (truth == ABSTRACT_TRUE || truth == ABSTRACT_FALSE)
        return c_type_name(c_expression_type(context, child(node, truth == ABSTRACT_TRUE ? 1 : 2)));
    c_value_type_t a = c_expression_type(context, child(node, 1));
    return c_type_name(c_expression_type(context, child(node, 0))) && c_type_name(a)
           && a == c_expression_type(context, child(node, 2));
}

static node_vtbl_t vtbl = {.type = NODE_CONDITIONAL,
                           .type_name = U"conditional expression",
                           .analyze_reachability = reach,
                           .is_pure = children_are_pure,
                           .simplify = simplify_constant_expression,
                           .collect_direct_effects = collect_child_effects,
                           .get_data = no_data,
                           .get_property_count = no_properties,
                           .get_property = no_property,
                           .get_child_count = count,
                           .get_child = child,
                           .get_child_tag = tag,
                           .insert_child_before = no_child_insertion,
                           .replace_child = replace,
                           .get_related_count = no_related_nodes,
                           .get_related = no_related_node,
                           .get_relation_type = no_relation_type,
                           .calculate = calculate,
                           .execute = execute_nothing,
                           .generate_goat_code = source,
                           .generate_indented_goat_code = indented_source,
                           .generate_bytecode = bytecode,
                           .can_generate_c_code = native,
                           .generate_c_code = c_conditional,
                           .generate_indented_c_code = no_indented_c_code,
                           .generate_bytecode_assign = no_bytecode_assignment,
                           .generate_bytecode_deferred = no_deferred_bytecode};

expression_t *create_conditional_node(arena_t *arena,
                                      expression_t *condition,
                                      expression_t *when_true,
                                      expression_t *when_false) {
    conditional_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
    expr->base.base.vtbl = &vtbl;
    expr->parts[0] = condition;
    expr->parts[1] = when_true;
    expr->parts[2] = when_false;
    return &expr->base;
}
