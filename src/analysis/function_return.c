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

/** @brief Same-body signatures and recursive components are solved together. */
typedef struct recursive_type_group_t {
    function_call_graph_node_t **members;
    size_t count;
} recursive_type_group_t;

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
                                                   recursive_type_group_t *group,
                                                   bool *incomplete,
                                                   c_expression_context_t *expressions) {
    abstract_state_t *state = create_abstract_state(set->arena);
    state->type_analysis_incomplete = incomplete;
    state->c_expressions = expressions;
    state->recursive_group = group;
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
    const lattice_element_t *result =
        evaluate_signature(set, summary, NULL, NULL, &incomplete, NULL);
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
                       NULL,
                       &incomplete,
                       NULL);
}

/** @brief Coverage between normalized type domains, not concrete argument values. */
static bool covers_type(lattice_type_t domain, lattice_type_t value) {
    return domain == value || domain == LATTICE_TOP
           || (domain == LATTICE_NOT_NULL && value != LATTICE_TOP && value != LATTICE_NULL)
           || (domain == LATTICE_NUMERIC && (value == LATTICE_INTEGER || value == LATTICE_REAL));
}

/** @brief Prefer an exact key; a broader existing signature is a safe fallback. */
static function_summary_t *
find_signature(function_summary_set_t *set, const lattice_element_t *const *args, size_t count) {
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

const lattice_element_t *interpret_recursive_call(const node_t *site,
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
    recursive_type_group_t *group = state->recursive_group;
    function_summary_t *target =
        function ? find_signature(get_function_summaries(function), args, count) : NULL;
    bool member = false;
    for (size_t i = 0; i < group->count; i++)
        member |= group->members[i]->summary == target;
    if (!member)
        target = NULL;
    if (!target || target->status == FUNCTION_INCONCLUSIVE) {
        *state->type_analysis_incomplete = true;
        forget_abstract_values(state);
        return make_top_element();
    }
    /* Another body may be a closure that can write the caller's locals. */
    if (function == state->call_graph_node->summary->function)
        forget_captured_abstract_values(state, function);
    else
        forget_abstract_values(state);
    if (target->return_type->type == LATTICE_BOTTOM)
        state->control_flow = FLOW_UNREACHABLE;
    return target->return_type;
}

/** @brief Monotone iteration over an entire recursive type group. */
static void solve_group(recursive_type_group_t *group, size_t max_iterations) {
    for (size_t i = 0; i < group->count; i++) {
        function_summary_t *s = group->members[i]->summary;
        reset_function_summary(s);
        s->status = FUNCTION_ANALYZING;
        s->return_type = make_bottom_element();
    }
    for (size_t iteration = 0; iteration < max_iterations; iteration++) {
        bool changed = false;
        for (size_t i = 0; i < group->count; i++) {
            function_call_graph_node_t *node = group->members[i];
            function_summary_t *s = node->summary;
            if (s->status == FUNCTION_INCONCLUSIVE)
                continue;
            function_summary_set_t *set = get_function_summaries(s->function);
            bool incomplete = false;
            s->iterations++;
            const lattice_element_t *result =
                evaluate_signature(set, s, node, group, &incomplete, NULL);
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
            for (size_t i = 0; i < group->count; i++) {
                function_summary_t *s = group->members[i]->summary;
                if (s->status == FUNCTION_ANALYZING)
                    s->status = FUNCTION_ANALYZED;
            }
            return;
        }
    }
    for (size_t i = 0; i < group->count; i++) {
        function_summary_t *s = group->members[i]->summary;
        s->status = FUNCTION_INCONCLUSIVE;
        s->return_type = make_top_element();
    }
}

static bool contains(const recursive_type_group_t *group, const function_call_graph_node_t *node) {
    for (size_t i = 0; i < group->count; i++) {
        if (group->members[i] == node)
            return true;
    }
    return false;
}

/** @brief Include every profile of a body, closing over its recursive components. */
static void expand_group(recursive_type_group_t *group, const function_call_graph_t *graph) {
    for (size_t i = 0; i < group->count; i++) {
        const function_call_graph_node_t *member = group->members[i];
        for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
            if ((node->component == member->component
                 || node->summary->function == member->summary->function)
                && !contains(group, node))
                group->members[group->count++] = node;
        }
    }
}

void solve_function_recursion(function_call_graph_t *graph, size_t max_iterations) {
    if (graph->truncated)
        return;
    recursive_type_group_t done = {
        .members = alloc_from_arena(graph->arena, (graph->count + 1) * sizeof(*done.members))};
    recursive_type_group_t group = {
        .members = alloc_from_arena(graph->arena, (graph->count + 1) * sizeof(*group.members))};
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        if (!node->recursive || contains(&done, node))
            continue;
        group.count = 1;
        group.members[0] = node;
        expand_group(&group, graph);
        solve_group(&group, max_iterations);
        for (size_t i = 0; i < group.count; i++)
            done.members[done.count++] = group.members[i];
    }
}

void analyze_function_c_expressions(node_t *root) {
    if (root->vtbl->type == NODE_FUNCTION_OBJECT) {
        function_summary_set_t *set = get_function_summaries(root);
        for (function_summary_t *summary = set->head; summary; summary = summary->next) {
            c_expression_context_t context = {.arena = set->arena};
            bool incomplete = false;
            evaluate_signature(set, summary, NULL, NULL, &incomplete, &context);
            summary->c_expressions = context.head;
        }
    }
    for (size_t i = 0; i < get_node_child_count(root); i++)
        analyze_function_c_expressions(get_node_child(root, i));
}

const lattice_element_t *evaluate_function_c_signature(function_summary_t *summary,
                                                       c_expression_context_t *context,
                                                       bool *incomplete) {
    return evaluate_signature(get_function_summaries(summary->function),
                              summary,
                              NULL,
                              NULL,
                              incomplete,
                              context);
}
