/** @file reachability.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Common traversal and lifetime of immediate-execution proofs.
 */
#include "reachability.h"

#include "graph/expression.h"
#include "graph/node.h"
#include "graph/statement.h"
#include "simplification.h"

/** @brief Marks a dead subtree, or resets proofs before a fresh traversal. */
static void set_subtree_flag(node_t *node, bool unreachable) {
    if (is_expression(node->vtbl->type))
        ((expression_t *)node)->immediate_value = NULL;
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
void mark_unreachable_subtree(node_t *node, analysis_collector_t *collector) {
    set_subtree_flag(node, true);
    add_analysis_event(collector, ANALYSIS_UNREACHABLE, node, NULL, NULL);
}

const lattice_element_t *
visit_reachable_node(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    if ((*state)->control_flow != FLOW_NORMAL) {
        mark_unreachable_subtree(node, collector);
        return make_bottom_element();
    }
    const lattice_element_t *value = node->vtbl->analyze_reachability(node, state, collector);
    if ((*state)->control_flow == FLOW_NORMAL && can_generate_c_code_from_node(node, value))
        node->flags |= NODE_FLAG_C_COMPATIBLE;
    if (is_expression(node->vtbl->type) && (*state)->control_flow == FLOW_NORMAL)
        ((expression_t *)node)->immediate_value = value;
    return value;
}

void mark_unreachable_code(node_t *root, arena_t *arena, analysis_collector_t *collector) {
    restore_graph(root);
    set_subtree_flag(root, false);
    abstract_state_t *state = create_abstract_state(arena);
    visit_reachable_node(root, &state, collector);
    destroy_abstract_state(state);
}
