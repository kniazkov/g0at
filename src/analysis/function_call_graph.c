/** @file function_call_graph.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Call discovery and Tarjan components, independent of result propagation.
 */
#include "function_call_graph.h"

#include "function_return.h"
#include "graph/expression.h"
#include "graph/variable.h"

node_t *resolve_immutable_function(const node_t *expression) {
    /* Cyclic aliases and excessively long chains remain unknown. */
    for (size_t depth = 0; expression && depth < 64; depth++) {
        if (expression->vtbl->type == NODE_FUNCTION_OBJECT)
            return (node_t *)expression;
        if (expression->vtbl->type == NODE_EXPRESSION_PARENTHESIZED) {
            expression = get_node_child(expression, 0);
        } else if (expression->vtbl->type == NODE_VARIABLE) {
            const declarator_t *binding = ((const variable_t *)expression)->declarator;
            if (!binding || binding == get_builtin_declarator())
                return NULL;
            const node_t *decl = &binding->base;
            if (decl->vtbl->type != NODE_CONSTANT_DECLARATOR)
                return NULL;
            expression = get_node_child(decl, 0);
        } else {
            return NULL;
        }
    }
    return NULL;
}

static function_call_graph_node_t *add_node(function_call_graph_t *graph,
                                            function_summary_t *summary) {
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        if (node->summary == summary)
            return node;
    }
    if (graph->count == graph->limit) {
        graph->truncated = true;
        return NULL;
    }
    function_call_graph_node_t *node = alloc_zeroed_from_arena(graph->arena, sizeof(*node));
    node->graph = graph;
    node->summary = summary;
    node->id = ++graph->count;
    node->complete = true;
    if (graph->tail)
        graph->tail->next = node;
    else
        graph->head = node;
    graph->tail = node;
    return node;
}

void observe_function_call(function_call_graph_node_t *caller,
                           const node_t *site,
                           const lattice_element_t *callee,
                           const lattice_element_t *const *args,
                           size_t count) {
    if (!caller)
        return;
    for (size_t i = 0; i < count; i++) {
        if (args[i]->type == LATTICE_BOTTOM)
            return;
    }
    node_t *function = NULL;
    if (callee->type == LATTICE_KNOWN_FUNCTION) {
        const known_function_element_t *known = (const known_function_element_t *)callee;
        function = known->node;
    } else if (callee->type == LATTICE_TOP || callee->type == LATTICE_FUNCTION
               || callee->type == LATTICE_NOT_NULL) {
        function = resolve_immutable_function(get_node_child(site, 0));
    }
    function_call_graph_node_t *target = NULL;
    call_target_kind_t kind = CALL_TARGET_UNKNOWN;
    if (function) {
        function_summary_t *summary =
            register_function_specialization(get_function_summaries(function), args, count);
        target = add_node(caller->graph, summary);
        kind = target ? CALL_TARGET_USER : CALL_TARGET_LIMIT;
    }
    if (!target)
        caller->complete = false;
    for (function_call_edge_t *edge = caller->edges; edge; edge = edge->next) {
        if (edge->site == site && edge->target == target && edge->kind == kind)
            return;
    }
    function_call_edge_t *edge = alloc_zeroed_from_arena(caller->graph->arena, sizeof(*edge));
    edge->site = site;
    edge->target = target;
    edge->kind = kind;
    if (caller->tail)
        caller->tail->next = edge;
    else
        caller->edges = edge;
    caller->tail = edge;
}

static void seed_nodes(node_t *root, function_call_graph_t *graph) {
    if (root->vtbl->type == NODE_FUNCTION_OBJECT) {
        for (function_summary_t *s = get_function_summaries(root)->head; s; s = s->next)
            add_node(graph, s);
    }
    for (size_t i = 0; i < get_node_child_count(root); i++)
        seed_nodes(get_node_child(root, i), graph);
}

/** @brief Extracts strongly connected components of the known-target subgraph. */
static void visit_component(function_call_graph_node_t *node,
                            size_t *index,
                            function_call_graph_node_t **stack) {
    node->index = node->lowlink = ++*index;
    node->stack_next = *stack;
    *stack = node;
    node->on_stack = true;
    bool self = false;
    for (function_call_edge_t *edge = node->edges; edge; edge = edge->next) {
        function_call_graph_node_t *target = edge->target;
        if (!target)
            continue;
        self |= target == node;
        if (!target->index) {
            visit_component(target, index, stack);
            if (target->lowlink < node->lowlink)
                node->lowlink = target->lowlink;
        } else if (target->on_stack && target->index < node->lowlink) {
            node->lowlink = target->index;
        }
    }
    if (node->index == node->lowlink) {
        size_t component = ++node->graph->component_count;
        size_t count = 0;
        function_call_graph_node_t *first = *stack;
        function_call_graph_node_t *item;
        do {
            item = *stack;
            *stack = item->stack_next;
            item->on_stack = false;
            item->component = component;
            count++;
        } while (item != node);
        for (item = first;; item = item->stack_next) {
            item->component_size = count;
            item->recursive = count > 1 || self;
            if (item == node)
                break;
        }
    }
}

function_call_graph_t *build_function_call_graph(node_t *root, arena_t *arena, size_t limit) {
    function_call_graph_t *graph = alloc_zeroed_from_arena(arena, sizeof(*graph));
    graph->arena = arena;
    graph->limit = limit;
    seed_nodes(root, graph);
    /* Appending discovered signatures makes this list a work queue. */
    for (function_call_graph_node_t *node = graph->head; node; node = node->next)
        inspect_function_calls(node);
    size_t index = 0;
    function_call_graph_node_t *stack = NULL;
    for (function_call_graph_node_t *node = graph->head; node; node = node->next) {
        if (!node->index)
            visit_component(node, &index, &stack);
    }
    return graph;
}
