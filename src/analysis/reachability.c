/**
 * @file reachability.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Reachability proofs independent of experimental arithmetic summaries.
 */
#include "reachability.h"

#include "abstract_state.h"
#include "addition.h"
#include "bitwise.h"
#include "comparison.h"
#include "division.h"
#include "function_call.h"
#include "graph/comparison.h"
#include "graph/declarations.h"
#include "graph/logic.h"
#include "graph/node.h"
#include "graph/statement.h"
#include "graph/update_expression.h"
#include "graph/variable.h"
#include "lattice.h"
#include "model/builtin_function.h"
#include "modulo.h"
#include "multiplication.h"
#include "power.h"
#include "subtraction.h"
#include "unary_operation.h"
#include "update.h"

/** @brief Marks a dead subtree, or resets proofs before a fresh traversal. */
static void set_subtree_flag(node_t *node, bool unreachable) {
    if (unreachable)
        node->flags |= NODE_FLAG_UNREACHABLE;
    else
        node->flags &= ~(NODE_FLAG_UNREACHABLE | NODE_FLAG_PURE | NODE_FLAG_C_COMPATIBLE);
    if (!unreachable && node->vtbl->type == NODE_IF_ELSE) {
        set_if_else_condition_truth(node, ABSTRACT_EITHER);
    }
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        set_subtree_flag(get_node_child(node, i), unreachable);
    }
}

/** @brief Marks a whole subtree unreachable and records one event for its root. */
static void mark_dead(node_t *node, analysis_collector_t *collector) {
    set_subtree_flag(node, true);
    add_analysis_event(collector, ANALYSIS_UNREACHABLE, node, NULL, NULL);
}

static const lattice_element_t *
visit(node_t *node, abstract_state_t **state, analysis_collector_t *collector);

/** @brief Visits children in order, marking those after terminated control flow unreachable. */
static void
visit_children(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        visit(get_node_child(node, i), state, collector);
    }
}

/** @brief Marks excluded branches and merges states when either branch may execute. */
static void visit_if(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    const lattice_element_t *condition = visit(get_node_child(node, 0), state, collector);
    node_t *yes = get_node_child(node, 1);
    node_t *no = get_node_child_count(node) == 3 ? get_node_child(node, 2) : NULL;
    if ((*state)->control_flow != FLOW_NORMAL) {
        set_if_else_condition_truth(node, ABSTRACT_NEVER);
        mark_dead(yes, collector);
        if (no)
            mark_dead(no, collector);
        return;
    }
    abstract_truth_t truth = lattice_truth(condition);
    set_if_else_condition_truth(node, truth);
    if (truth == ABSTRACT_TRUE) {
        if (no)
            mark_dead(no, collector);
        visit(yes, state, collector);
    } else if (truth == ABSTRACT_FALSE) {
        mark_dead(yes, collector);
        if (no)
            visit(no, state, collector);
    } else {
        abstract_state_t *left = clone_abstract_state(*state);
        abstract_state_t *right = clone_abstract_state(*state);
        visit(yes, &left, collector);
        if (no)
            visit(no, &right, collector);
        abstract_state_t *merged = join_abstract_states(left, right);
        destroy_abstract_state(left);
        destroy_abstract_state(right);
        destroy_abstract_state(*state);
        *state = merged;
    }
}

/** @brief Visits the right operand only on paths which do not short-circuit. */
static const lattice_element_t *
visit_logical(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    const lattice_element_t *left = visit(get_node_child(node, 0), state, collector);
    node_t *right_node = get_node_child(node, 1);
    if ((*state)->control_flow != FLOW_NORMAL) {
        mark_dead(right_node, collector);
        return make_bottom_element();
    }
    bool is_or = node->vtbl->type == NODE_LOGICAL_OR;
    abstract_truth_t truth = lattice_truth(left);
    const lattice_element_t *skipped = is_or ? make_true_element() : make_false_element();
    if (truth == (is_or ? ABSTRACT_TRUE : ABSTRACT_FALSE)) {
        mark_dead(right_node, collector);
        return skipped;
    }
    if (truth != ABSTRACT_EITHER)
        return lattice_boolean(visit(right_node, state, collector), false);
    abstract_state_t *right = clone_abstract_state(*state);
    const lattice_element_t *value = lattice_boolean(visit(right_node, &right, collector), false);
    if (right->control_flow != FLOW_NORMAL)
        value = make_bottom_element();
    abstract_state_t *merged = join_abstract_states(*state, right);
    destroy_abstract_state(right);
    destroy_abstract_state(*state);
    *state = merged;
    return lattice_join(merged->arena, skipped, value);
}

/**
 * @brief Updates the abstract state, marks dead subtrees, and returns the node's abstract value.
 * Branch merging may replace *state; deferred function bodies are skipped.
 */
static const lattice_element_t *
visit(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    if ((*state)->control_flow != FLOW_NORMAL) {
        mark_dead(node, collector);
        return make_bottom_element();
    }
    switch (node->vtbl->type) {
        case NODE_NULL:
        case NODE_FALSE:
        case NODE_TRUE:
        case NODE_INTEGER:
        case NODE_REAL:
        case NODE_STATIC_STRING:
            return calculate_node(node, *state, (*state)->arena);
        case NODE_FUNCTION_OBJECT:
            /* Creation is immediate; its body may run later with different captures. */
            return make_function_element();
        case NODE_VARIABLE: {
            const declarator_t *decl = ((variable_t *)node)->declarator;
            if (decl == get_builtin_declarator())
                return calculate_node(node, *state, (*state)->arena);
            const lattice_element_t *value = get_from_abstract_state(*state, decl);
            if (is_integer_lattice_element(value) || is_real_lattice_element(value))
                node->flags |= NODE_FLAG_C_COMPATIBLE;
            return value ? value : make_top_element();
        }
        case NODE_EXPRESSION_PARENTHESIZED:
        case NODE_STATEMENT_EXPRESSION:
            return visit(get_node_child(node, 0), state, collector);
        case NODE_ROOT:
        case NODE_STATEMENT_LIST:
        case NODE_VARIABLE_DECLARATION:
        case NODE_CONSTANT_DECLARATION:
            visit_children(node, state, collector);
            return make_top_element();
        case NODE_VARIABLE_DECLARATOR:
        case NODE_CONSTANT_DECLARATOR: {
            const lattice_element_t *value = get_node_child_count(node)
                                                 ? visit(get_node_child(node, 0), state, collector)
                                                 : make_null_element();
            if ((*state)->control_flow == FLOW_NORMAL) {
                set_in_abstract_state(*state, (declarator_t *)node, value);
            }
            return value;
        }
        case NODE_SIMPLE_ASSIGNMENT: {
            const lattice_element_t *value = visit(get_node_child(node, 1), state, collector);
            node_t *target = get_node_child(node, 0);
            if ((*state)->control_flow == FLOW_NORMAL) {
                if (target->vtbl->type == NODE_VARIABLE) {
                    set_in_abstract_state(*state, ((variable_t *)target)->declarator, value);
                } else {
                    forget_abstract_values(*state);
                }
            }
            return value;
        }
        case NODE_IF_ELSE:
            visit_if(node, state, collector);
            return make_top_element();
        case NODE_THROW:
            visit_children(node, state, collector);
            (*state)->control_flow = FLOW_UNREACHABLE;
            return make_bottom_element();
        case NODE_RETURN:
            visit_children(node, state, collector);
            (*state)->control_flow = FLOW_RETURN;
            return make_bottom_element();
        case NODE_FUNCTION_CALL: {
            size_t count = get_node_child_count(node) - 1;
            const lattice_element_t **args =
                alloc_from_arena((*state)->arena, (count + 1) * sizeof(*args));
            for (size_t i = count; i > 0; i--)
                args[i - 1] = visit(get_node_child(node, i), state, collector);
            const lattice_element_t *callee = visit(get_node_child(node, 0), state, collector);
            if ((*state)->control_flow != FLOW_NORMAL)
                return make_bottom_element();
            if (callee->type == LATTICE_KNOWN_FUNCTION
                && ((const known_function_element_t *)callee)->builtin) {
                const builtin_function_t *builtin =
                    ((const known_function_element_t *)callee)->builtin;
                if (builtin->effects == BUILTIN_EFFECT_NONE)
                    node->flags |= NODE_FLAG_PURE;
                return interpret_function_call(callee, args, count, *state);
            }
            forget_abstract_values(*state);
            return make_top_element();
        }
        case NODE_LOGICAL_AND:
        case NODE_LOGICAL_OR:
            return visit_logical(node, state, collector);
        case NODE_LOGICAL_NOT:
        case NODE_BOOLEAN_CONVERSION:
        case NODE_BITWISE_NOT: {
            const lattice_element_t *value = visit(get_node_child(node, 0), state, collector);
            value = node->vtbl->type == NODE_BITWISE_NOT
                        ? lattice_bitwise_not((*state)->arena, value)
                        : lattice_boolean(value, node->vtbl->type == NODE_LOGICAL_NOT);
            if (value->type == LATTICE_BOTTOM)
                (*state)->control_flow = FLOW_UNREACHABLE;
            return value;
        }
        case NODE_BITWISE_AND:
        case NODE_BITWISE_OR:
        case NODE_BITWISE_XOR:
        case NODE_SHIFT_LEFT:
        case NODE_SHIFT_RIGHT: {
            const lattice_element_t *left = visit(get_node_child(node, 0), state, collector);
            const lattice_element_t *right = visit(get_node_child(node, 1), state, collector);
            const lattice_element_t *value =
                lattice_bitwise((*state)->arena, left, right, node_bitwise_kind(node->vtbl->type));
            if (value->type == LATTICE_BOTTOM)
                (*state)->control_flow = FLOW_UNREACHABLE;
            return value;
        }
        case NODE_PREFIX_INCREMENT:
        case NODE_PREFIX_DECREMENT:
        case NODE_POSTFIX_INCREMENT:
        case NODE_POSTFIX_DECREMENT: {
            node_t *target = get_node_child(node, 0);
            const lattice_element_t *old = visit(target, state, collector);
            const lattice_element_t *value =
                lattice_update((*state)->arena, old, update_is_decrement(node->vtbl->type));
            const declarator_t *decl = ((variable_t *)target)->declarator;
            if (decl && decl != get_builtin_declarator()
                && decl->base.vtbl->type == NODE_CONSTANT_DECLARATOR)
                value = make_bottom_element();
            if (value->type == LATTICE_BOTTOM)
                (*state)->control_flow = FLOW_UNREACHABLE;
            if ((*state)->control_flow != FLOW_NORMAL)
                return make_bottom_element();
            if (decl != get_builtin_declarator())
                set_in_abstract_state(*state, decl, value);
            return update_is_postfix(node->vtbl->type) ? old : value;
        }
        case NODE_UNARY_PLUS:
        case NODE_UNARY_MINUS: {
            const lattice_element_t *value = visit(get_node_child(node, 0), state, collector);
            const lattice_element_t *result =
                lattice_unary((*state)->arena, value, node->vtbl->type == NODE_UNARY_MINUS);
            if (result->type == LATTICE_BOTTOM)
                (*state)->control_flow = FLOW_UNREACHABLE;
            return result;
        }
        case NODE_ADDITION:
        case NODE_SUBTRACTION:
        case NODE_MULTIPLICATION:
        case NODE_DIVISION:
        case NODE_POWER:
        case NODE_MODULO: {
            const lattice_element_t *left = visit(get_node_child(node, 0), state, collector);
            const lattice_element_t *right = visit(get_node_child(node, 1), state, collector);
            const lattice_element_t *result;
            switch (node->vtbl->type) {
                case NODE_ADDITION:
                    result = lattice_add((*state)->arena, left, right);
                    break;
                case NODE_SUBTRACTION:
                    result = lattice_subtract((*state)->arena, left, right);
                    break;
                case NODE_MULTIPLICATION:
                    result = lattice_multiply((*state)->arena, left, right);
                    break;
                case NODE_DIVISION:
                    result = lattice_divide((*state)->arena, left, right);
                    break;
                case NODE_POWER:
                    result = lattice_power((*state)->arena, left, right);
                    break;
                default:
                    result = lattice_modulo((*state)->arena, left, right);
                    break;
            }
            if (result->type == LATTICE_BOTTOM)
                (*state)->control_flow = FLOW_UNREACHABLE;
            return result;
        }
        case NODE_LESS:
        case NODE_LESS_OR_EQUAL:
        case NODE_GREATER:
        case NODE_GREATER_OR_EQUAL:
        case NODE_EQUAL:
        case NODE_NOT_EQUAL: {
            const lattice_element_t *left = visit(get_node_child(node, 0), state, collector);
            const lattice_element_t *right = visit(get_node_child(node, 1), state, collector);
            const lattice_element_t *result =
                lattice_compare(left, right, node_comparison_kind(node->vtbl->type));
            if (result->type == LATTICE_BOTTOM)
                (*state)->control_flow = FLOW_UNREACHABLE;
            return result;
        }
        default:
            /* Unknown control flow (including future loops) is not traversed once. */
            forget_abstract_values(*state);
            return make_top_element();
    }
}

/** @brief Clears old marks and checks immediate execution from a fresh abstract state. */
void mark_unreachable_code(node_t *root, arena_t *arena, analysis_collector_t *collector) {
    set_subtree_flag(root, false);
    abstract_state_t *state = create_abstract_state(arena);
    visit(root, &state, collector);
    destroy_abstract_state(state);
}
