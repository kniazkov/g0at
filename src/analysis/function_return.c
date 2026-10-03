/** @file function_return.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Return-type analysis independent of concrete call observations.
 */
#include "function_return.h"

#include "abstract_state.h"
#include "function_call_graph.h"
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
static const lattice_element_t *evaluate_signature(function_summary_set_t *set,
                                                   function_summary_t *summary,
                                                   function_call_graph_node_t *graph_node,
                                                   bool recursive,
                                                   bool *incomplete) {
    abstract_state_t *state = create_abstract_state(set->arena);
    state->type_analysis_incomplete = incomplete;
    state->recursive_signatures = recursive ? set : NULL;
    state->call_graph_node = graph_node;
    /* A type-only key does not prove that root bindings have not been replaced. */
    state->builtin_bindings_unknown = true;
    const lattice_element_t *result = make_bottom_element();
    state->return_value = &result;
    node_t *body = get_node_child(set->function, 1);
    if (!prepare_body(body, set->function, state)) {
        *incomplete = true;
        if (graph_node)
            graph_node->complete = false;
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
    destroy_abstract_state(state);
    return function_summary_type(result);
}

static void analyze_signature(function_summary_set_t *set, function_summary_t *summary) {
    reset_function_summary(summary);
    summary->status = FUNCTION_ANALYZING;
    bool incomplete = false;
    const lattice_element_t *result = evaluate_signature(set, summary, NULL, false, &incomplete);
    summary->status = incomplete ? FUNCTION_INCONCLUSIVE : FUNCTION_ANALYZED;
    summary->return_type = incomplete ? make_top_element() : result;
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

void inspect_function_calls(function_call_graph_node_t *node) {
    bool incomplete = false;
    evaluate_signature(get_function_summaries(node->summary->function),
                       node->summary,
                       node,
                       false,
                       &incomplete);
}

/** @brief Coverage between normalized type domains, not concrete argument values. */
static bool covers_type(lattice_type_t domain, lattice_type_t value) {
    return domain == value || domain == LATTICE_TOP
           || (domain == LATTICE_NOT_NULL && value != LATTICE_TOP && value != LATTICE_NULL)
           || (domain == LATTICE_NUMERIC && (value == LATTICE_INTEGER || value == LATTICE_REAL));
}

/** @brief Prefer an exact key; a broader existing signature is a safe fallback. */
static function_summary_t *find_self_signature(function_summary_set_t *set,
                                               const lattice_element_t *const *args,
                                               size_t count) {
    function_summary_t *fallback = NULL;
    for (function_summary_t *s = set->head; s; s = s->next) {
        bool exact = true, covers = true;
        for (size_t i = 0; i < s->parameter_count; i++) {
            lattice_type_t value =
                function_summary_type(i < count ? args[i] : make_null_element())->type;
            lattice_type_t domain = s->parameter_types[i]->type;
            exact &= domain == value;
            covers &= covers_type(domain, value);
        }
        if (exact)
            return s;
        if (covers && !fallback)
            fallback = s;
    }
    return fallback;
}

const lattice_element_t *interpret_self_call(const node_t *site,
                                             const lattice_element_t *callee,
                                             const lattice_element_t *const *args,
                                             size_t count,
                                             abstract_state_t *state) {
    for (size_t i = 0; i < count; i++) {
        if (args[i]->type == LATTICE_BOTTOM) {
            state->control_flow = FLOW_UNREACHABLE;
            return make_bottom_element();
        }
    }
    node_t *function = NULL;
    if (callee->type == LATTICE_KNOWN_FUNCTION)
        function = ((const known_function_element_t *)callee)->node;
    else if (callee->type == LATTICE_TOP || callee->type == LATTICE_NOT_NULL
             || callee->type == LATTICE_FUNCTION)
        function = resolve_immutable_function(get_node_child(site, 0));
    function_summary_set_t *set = state->recursive_signatures;
    function_summary_t *target =
        function == set->function ? find_self_signature(set, args, count) : NULL;
    if (!target || target->status == FUNCTION_INCONCLUSIVE) {
        *state->type_analysis_incomplete = true;
        forget_abstract_values(state);
        return make_top_element();
    }
    /* A self-call gets a fresh activation; only shared captures may change. */
    forget_captured_abstract_values(state, function);
    if (target->return_type->type == LATTICE_BOTTOM)
        state->control_flow = FLOW_UNREACHABLE;
    return target->return_type;
}

static void fail_signatures(function_summary_set_t *set) {
    for (function_summary_t *s = set->head; s; s = s->next) {
        s->status = FUNCTION_INCONCLUSIVE;
        s->return_type = make_top_element();
    }
}

/** @brief All specializations of one body share a finite-height, type-only iteration. */
static void solve_function(function_summary_set_t *set, size_t max_iterations) {
    for (function_summary_t *s = set->head; s; s = s->next) {
        reset_function_summary(s);
        s->status = FUNCTION_ANALYZING;
        s->return_type = make_bottom_element();
    }
    for (size_t iteration = 0; iteration < max_iterations; iteration++) {
        bool changed = false;
        for (function_summary_t *s = set->head; s; s = s->next) {
            if (s->status == FUNCTION_INCONCLUSIVE)
                continue;
            bool incomplete = false;
            s->iterations++;
            const lattice_element_t *result = evaluate_signature(set, s, NULL, true, &incomplete);
            if (incomplete) {
                s->status = FUNCTION_INCONCLUSIVE;
                s->return_type = make_top_element();
                changed = true;
            } else {
                const lattice_element_t *joined =
                    function_summary_type(lattice_join(set->arena, s->return_type, result));
                changed |= joined->type != s->return_type->type;
                s->return_type = joined;
            }
        }
        if (!changed) {
            for (function_summary_t *s = set->head; s; s = s->next) {
                if (s->status == FUNCTION_ANALYZING)
                    s->status = FUNCTION_ANALYZED;
            }
            return;
        }
    }
    fail_signatures(set);
}

/** @brief A recursive component must contain only specializations of this same body. */
static bool has_direct_cycle(const function_call_graph_t *graph, const node_t *function) {
    for (const function_call_graph_node_t *node = graph->head; node; node = node->next) {
        if (node->summary->function != function || !node->recursive)
            continue;
        bool same_body = true;
        for (const function_call_graph_node_t *other = graph->head; other; other = other->next) {
            if (other->component == node->component && other->summary->function != function)
                same_body = false;
        }
        if (same_body)
            return true;
    }
    return false;
}

void solve_direct_function_recursion(function_call_graph_t *graph, size_t max_iterations) {
    if (graph->truncated)
        return;
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        const node_t *function = node->summary->function;
        bool seen = false;
        for (function_call_graph_node_t *prior = graph->head; prior != node; prior = prior->next)
            seen |= prior->summary->function == function;
        if (!seen && has_direct_cycle(graph, function))
            solve_function(get_function_summaries(function), max_iterations);
    }
}
