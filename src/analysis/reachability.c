/**
 * @file reachability.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Reachability proofs independent of experimental arithmetic summaries.
 */
#include "reachability.h"
#include "abstract_state.h"
#include "lattice.h"
#include "graph/node.h"
#include "graph/variable.h"
#include "graph/declarations.h"

/** @brief Sets or clears the unreachable flag on a node and all descendants. */
static void set_subtree_flag(node_t *node, bool unreachable) {
    node->unreachable = unreachable;
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        set_subtree_flag(get_node_child(node, i), unreachable);
    }
}

/** @brief Marks a whole subtree unreachable and records one event for its root. */
static void mark_dead(node_t *node, analysis_collector_t *collector) {
    set_subtree_flag(node, true);
    add_analysis_event(collector, ANALYSIS_UNREACHABLE, node, NULL, NULL);
}

/** @brief AVL callback: replaces one variable value with TOP. */
static void forget_entry(void *context, void *key, value_t ignored) {
    set_in_abstract_state(context, key, make_top_element());
}

/** @brief Discards variable facts after effects that this pass cannot track. */
static void forget_values(abstract_state_t *state) {
    avl_tree_for_each(state->values, forget_entry, state);
}

static const lattice_element_t *visit(node_t *node, abstract_state_t **state,
        analysis_collector_t *collector);

/** @brief Visits children in order, marking those after terminated control flow unreachable. */
static void visit_children(node_t *node, abstract_state_t **state,
        analysis_collector_t *collector) {
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
        mark_dead(yes, collector);
        if (no) mark_dead(no, collector);
        return;
    }
    abstract_truth_t truth = lattice_truth(condition);
    if (truth == ABSTRACT_TRUE) {
        if (no) mark_dead(no, collector);
        visit(yes, state, collector);
    } else if (truth == ABSTRACT_FALSE) {
        mark_dead(yes, collector);
        if (no) visit(no, state, collector);
    } else {
        abstract_state_t *left = clone_abstract_state(*state);
        abstract_state_t *right = clone_abstract_state(*state);
        visit(yes, &left, collector);
        if (no) visit(no, &right, collector);
        abstract_state_t *merged = join_abstract_states(left, right);
        destroy_abstract_state(left);
        destroy_abstract_state(right);
        destroy_abstract_state(*state);
        *state = merged;
    }
}

/**
 * @brief Updates the abstract state, marks dead subtrees, and returns the node's abstract value.
 * Branch merging may replace *state; deferred function bodies are skipped.
 */
static const lattice_element_t *visit(node_t *node, abstract_state_t **state,
        analysis_collector_t *collector) {
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
            if (decl == get_builtin_declarator()) return make_top_element();
            const lattice_element_t *value = get_from_abstract_state(*state, decl);
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
            const lattice_element_t *value = get_node_child_count(node) ?
                visit(get_node_child(node, 0), state, collector) : make_null_element();
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
                    forget_values(*state);
                }
            }
            return value;
        }
        case NODE_IF_ELSE:
            visit_if(node, state, collector);
            return make_top_element();
        case NODE_RETURN:
            visit_children(node, state, collector);
            (*state)->control_flow = FLOW_RETURN;
            return make_bottom_element();
        case NODE_FUNCTION_CALL:
            /* Match bytecode: arguments right-to-left, then the callee. */
            for (size_t i = get_node_child_count(node); i > 0; i--) {
                visit(get_node_child(node, i - 1), state, collector);
            }
            /* A closure or built-in may change any captured binding. */
            forget_values(*state);
            return make_top_element();
        case NODE_ADDITION:
        case NODE_SUBTRACTION:
        case NODE_MULTIPLICATION:
        case NODE_DIVISION:
        case NODE_MODULO:
        case NODE_POWER:
        case NODE_LESS:
        case NODE_LESS_OR_EQUAL:
        case NODE_GREATER:
        case NODE_GREATER_OR_EQUAL:
        case NODE_EQUAL:
        case NODE_NOT_EQUAL:
            visit_children(node, state, collector);
            /* Until abstract operators match the VM, their results are not proofs. */
            return make_top_element();
        default:
            /* Unknown control flow (including future loops) is not traversed once. */
            forget_values(*state);
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
