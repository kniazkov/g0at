/** @file function_call_graph.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Arena-owned may-call graph of type specializations; no recursive solving.
 */
#pragma once

#include "function_summary.h"

typedef struct function_call_graph_t function_call_graph_t;
typedef struct function_call_graph_node_t function_call_graph_node_t;

typedef enum { CALL_TARGET_USER, CALL_TARGET_UNKNOWN, CALL_TARGET_LIMIT } call_target_kind_t;

/** @brief One call site and possible target; unknown/limited targets have no target node. */
typedef struct function_call_edge_t {
    struct function_call_edge_t *next;
    const node_t *site;
    function_call_graph_node_t *target;
    call_target_kind_t kind;
} function_call_edge_t;

struct function_call_graph_node_t {
    function_call_graph_node_t *next;
    function_call_graph_t *graph;
    function_summary_t *summary;
    function_call_edge_t *edges;
    function_call_edge_t *tail;
    size_t id;
    size_t component;
    size_t component_size;
    bool recursive;
    bool complete; /**< Direct-call coverage, not a purity or termination proof. */
    /* Tarjan traversal state; not part of an analysis event. */
    size_t index, lowlink;
    bool on_stack;
    function_call_graph_node_t *stack_next;
};

struct function_call_graph_t {
    arena_t *arena;
    function_call_graph_node_t *head;
    function_call_graph_node_t *tail;
    size_t count, limit, component_count;
    bool truncated;
};

/** @brief Resolves literal functions through parentheses and immutable declaration aliases. */
node_t *resolve_immutable_function(const node_t *expression);

/** @brief Builds bounded call closure and strongly connected components from observed signatures.
 */
function_call_graph_t *build_function_call_graph(node_t *root, arena_t *arena, size_t limit);

/** @brief Records a reached call; argument values are normalized by the specialization registry. */
void observe_function_call(function_call_graph_node_t *caller,
                           const node_t *site,
                           const lattice_element_t *callee,
                           const lattice_element_t *const *args,
                           size_t count);
