/** @file c_body.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Bounded body proofs and static numeric call dependencies.
 */
#include "c_body.h"

#include "abstract_state.h"
#include "function_return.h"
#include "graph/expression.h"
#include "graph/variable.h"

/** @brief One local storage decision, shared by all paths of a signature. */
typedef struct c_binding_t {
    struct c_binding_t *next;
    const declarator_t *declaration;
    c_value_type_t type;
} c_binding_t;

bool c_body_node_supported(const node_t *node, const c_expression_context_t *context) {
    if (!context || !context->graph || !node)
        return false;
    if (is_expression(node->vtbl->type))
        return c_expression_type(context, node) != C_VALUE_UNKNOWN;
    return node->vtbl->can_generate_c_code && node->vtbl->can_generate_c_code(node, NULL, context);
}

bool c_body_children(const node_t *node,
                     const lattice_element_t *value,
                     const c_expression_context_t *context) {
    if (!context || !context->graph)
        return false;
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        if (!c_body_node_supported(get_node_child(node, i), context))
            return false;
    }
    return true;
}

bool c_local_binding(const c_expression_context_t *context,
                     const declarator_t *declaration,
                     c_value_type_t type) {
    if (!context || !context->graph || !declaration || declaration == get_builtin_declarator()
        || (type != C_VALUE_INT64 && type != C_VALUE_DOUBLE && type != C_VALUE_BOOL))
        return false;
    const node_t *owner = declaration->base.parent;
    while (owner && owner->vtbl->type != NODE_FUNCTION_OBJECT)
        owner = owner->parent;
    if (owner != context->summary->function)
        return false;
    for (const c_binding_t *binding = *context->bindings; binding; binding = binding->next) {
        if (binding->declaration == declaration)
            return binding->type == type;
    }
    c_binding_t *binding = alloc_from_arena(context->arena, sizeof(*binding));
    *binding = (c_binding_t){.next = *context->bindings, .declaration = declaration, .type = type};
    *context->bindings = binding;
    return true;
}

/** @brief Exact numeric signatures only; no callable identity or numeric-union coercions. */
static function_summary_t *call_target(const node_t *site,
                                       const lattice_element_t *const *args,
                                       size_t count,
                                       const c_expression_context_t *context) {
    const node_t *function = resolve_immutable_function(get_node_child(site, 0));
    if (!function)
        return NULL;
    for (function_call_graph_node_t *node = context->graph->head; node; node = node->next) {
        function_summary_t *s = node->summary;
        if (s->function != function || count < s->parameter_count || !function_summary_is_pure(s))
            continue;
        size_t i = 0;
        while (i < s->parameter_count && s->parameter_types[i] == function_summary_type(args[i]))
            i++;
        if (i == s->parameter_count && s->status == FUNCTION_ANALYZED
            && (classify_c_value_type(s->return_type->type) == C_VALUE_INT64
                || classify_c_value_type(s->return_type->type) == C_VALUE_DOUBLE))
            return s;
    }
    return NULL;
}

const lattice_element_t *interpret_c_call(const node_t *site,
                                          const lattice_element_t *const *args,
                                          size_t count,
                                          abstract_state_t *state) {
    c_expression_context_t *context = state->c_expressions;
    function_summary_t *target = call_target(site, args, count, context);
    c_call_t *call = alloc_from_arena(context->arena, sizeof(*call));
    *call = (c_call_t){.next = context->calls, .site = site, .target = target};
    context->calls = call;
    if (!target) {
        *state->type_analysis_incomplete = true;
        forget_abstract_values(state);
        return make_top_element();
    }
    /* Purity excludes writes into the caller; return domains carry no concrete-call facts. */
    return target->return_type;
}

bool c_call_supported(const node_t *site, const c_expression_context_t *context) {
    if (!context || !context->graph)
        return false;
    bool found = false;
    for (const c_call_t *call = context->calls; call; call = call->next) {
        if (call->site != site)
            continue;
        found = true;
        if (!call->target || call->target->c_support != FUNCTION_C_SUPPORTED)
            return false;
    }
    for (size_t i = 1; i < get_node_child_count(site); i++) {
        if (c_expression_type(context, get_node_child(site, i)) == C_VALUE_UNKNOWN)
            return false;
    }
    return found;
}

/** @brief Owns fresh facts for one evaluation; parameter storage has a fixed representation. */
static c_expression_context_t
make_context(function_call_graph_t *graph, function_summary_t *summary, c_binding_t **bindings) {
    c_expression_context_t context = {.arena = graph->arena,
                                      .graph = graph,
                                      .summary = summary,
                                      .bindings = bindings};
    const node_t *parameters = get_node_child(summary->function, 0);
    for (size_t i = 0; i < summary->parameter_count; i++)
        c_local_binding(&context,
                        (const declarator_t *)get_node_child(parameters, i),
                        classify_c_value_type(summary->parameter_types[i]->type));
    return context;
}

/** @brief Only improves previously unknown returns using independently proven pure callees. */
static bool refine_returns(function_call_graph_t *graph, size_t max_iterations) {
    for (size_t iteration = 0; iteration < max_iterations; iteration++) {
        bool changed = false;
        for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
            function_summary_t *s = node->summary;
            if (s->return_type->type != LATTICE_TOP || !function_summary_is_pure(s))
                continue;
            c_binding_t *bindings = NULL;
            c_expression_context_t context = make_context(graph, s, &bindings);
            bool incomplete = false;
            const lattice_element_t *value =
                evaluate_function_c_signature(s, &context, &incomplete);
            c_value_type_t type = classify_c_value_type(value->type);
            if (!incomplete && (type == C_VALUE_INT64 || type == C_VALUE_DOUBLE)) {
                s->return_type = value;
                s->status = FUNCTION_ANALYZED;
                changed = true;
            }
        }
        if (!changed)
            return true;
    }
    return false;
}

/** @brief Checks the entire syntax, including unvisited branches and statements. */
static bool check_body(function_call_graph_t *graph, function_summary_t *summary) {
    c_binding_t *bindings = NULL;
    c_expression_context_t context = make_context(graph, summary, &bindings);
    bool incomplete = false;
    const lattice_element_t *result = evaluate_function_c_signature(summary, &context, &incomplete);
    summary->c_expressions = context.head;
    summary->c_calls = context.calls;
    return !incomplete && result->type == summary->return_type->type
           && c_body_node_supported(get_node_child(summary->function, 1), &context);
}

void analyze_function_c_bodies(function_call_graph_t *graph, size_t max_iterations) {
    bool ready = !graph->truncated && refine_returns(graph, max_iterations);
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        check_function_c_contract(node->summary);
        node->summary->c_calls = NULL;
        if (!ready)
            node->summary->c_expressions = NULL;
        if (ready && node->summary->c_blockers == C_BLOCKER_BODY)
            node->summary->c_support = FUNCTION_C_SUPPORTED;
    }
    bool stable = false;
    if (ready) {
        for (size_t iteration = 0; iteration < max_iterations; iteration++) {
            bool changed = false;
            for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
                function_summary_t *s = node->summary;
                if (s->c_support == FUNCTION_C_SUPPORTED && !check_body(graph, s)) {
                    s->c_support = FUNCTION_C_UNKNOWN;
                    changed = true;
                }
            }
            if (!changed) {
                stable = true;
                break;
            }
        }
    }
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        function_summary_t *s = node->summary;
        if (!stable && s->c_support == FUNCTION_C_SUPPORTED)
            s->c_support = FUNCTION_C_UNKNOWN;
        if (s->c_support == FUNCTION_C_SUPPORTED)
            s->c_blockers &= ~C_BLOCKER_BODY;
    }
    /* Publish expressions only after tentative recursive assumptions are settled. */
    if (ready) {
        for (function_call_graph_node_t *node = graph->head; node; node = node->next)
            check_body(graph, node->summary);
    }
}
