/** @file function_return.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Return-type analysis independent of concrete call observations.
 */
#include "function_return.h"

#include "abstract_state.h"
#include "function_summary.h"
#include "graph/expression.h"
#include "graph/variable.h"

/** @brief Seeds external bindings with TOP; nested closure bodies are deferred. */
static bool prepare_body(node_t *node, const node_t *function, abstract_state_t *state) {
    if (node->vtbl->type == NODE_FUNCTION_OBJECT)
        return true;
    if (node->vtbl->type == NODE_TRY_CATCH)
        return false;
    if (node->vtbl->type == NODE_VARIABLE) {
        declarator_t *decl = ((variable_t *)node)->declarator;
        const node_t *owner = &decl->base;
        while (owner && owner->vtbl->type != NODE_FUNCTION_OBJECT)
            owner = owner->parent;
        if (owner != function && decl != get_builtin_declarator())
            set_in_abstract_state(state, decl, make_top_element());
    }
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        if (!prepare_body(get_node_child(node, i), function, state))
            return false;
    }
    return true;
}

/** @brief Joins normal returns; fallthrough contributes NULL, throwing paths contribute nothing. */
static void analyze_signature(function_summary_set_t *set, function_summary_t *summary) {
    reset_function_summary(summary);
    summary->status = FUNCTION_ANALYZING;
    abstract_state_t *state = create_abstract_state(set->arena);
    bool incomplete = false;
    state->type_analysis_incomplete = &incomplete;
    /* A type-only key does not prove that root bindings have not been replaced. */
    state->builtin_bindings_unknown = true;
    const lattice_element_t *result = make_bottom_element();
    state->return_value = &result;
    node_t *body = get_node_child(set->function, 1);
    if (!prepare_body(body, set->function, state)) {
        incomplete = true;
    } else {
        node_t *parameters = get_node_child(set->function, 0);
        for (size_t i = 0; i < summary->parameter_count; i++)
            set_in_abstract_state(state,
                                  (declarator_t *)get_node_child(parameters, i),
                                  summary->parameter_types[i]);
        for (size_t i = 0; i < get_node_child_count(body) && state->control_flow == FLOW_NORMAL;
             i++)
            state = execute_node(get_node_child(body, i), state, set->arena);
        if (state->control_flow == FLOW_NORMAL)
            result = lattice_join(set->arena, result, make_null_element());
    }
    summary->status = incomplete ? FUNCTION_INCONCLUSIVE : FUNCTION_ANALYZED;
    summary->return_type = incomplete ? make_top_element() : function_summary_type(result);
    destroy_abstract_state(state);
}

void analyze_function_return_types(node_t *root) {
    if (root->vtbl->type == NODE_FUNCTION_OBJECT) {
        function_summary_set_t *set = get_function_summaries(root);
        for (function_summary_t *summary = set->head; summary; summary = summary->next)
            analyze_signature(set, summary);
    }
    for (size_t i = 0; i < get_node_child_count(root); i++)
        analyze_function_return_types(get_node_child(root, i));
}
