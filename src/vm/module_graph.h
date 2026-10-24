/** @file module_graph.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Iterative, nonexecuting whole dependency graph preflight.
 */
#pragma once
#include "common/module_imports.h"
#include "module_search.h"

typedef enum {
    MODULE_GRAPH_OK,
    MODULE_GRAPH_ARGUMENT,
    MODULE_GRAPH_INVALID,
    MODULE_GRAPH_MEMORY
} module_graph_status_t;

typedef enum {
    MODULE_GRAPH_SEARCH,
    MODULE_GRAPH_HASH,
    MODULE_GRAPH_CONFLICT,
    MODULE_GRAPH_CYCLE,
    MODULE_GRAPH_REFERENCE
} module_graph_issue_kind_t;

typedef struct module_graph module_graph_t;

/** Borrowed immutable view. Node zero is the root; a source root has no
 * identity/snapshot. Failed nodes retain their requested identity and exact
 * search error; their dependencies are not invented. hash is the actual mainHash.
 */
typedef struct {
    const char *name, *version, *hash;
    const char *archive; /**< Owned diagnostic label, including failed/source roots. */
    const validated_module_t *snapshot;
    module_search_error_t search;
    size_t references_skipped; /**< Targets unresolved because a reachable edge failed. */
} module_graph_node_t;

typedef struct {
    size_t from, to;
    const char *hash; /**< Expected hash; source edges pin the actual validated hash. */
} module_graph_edge_t;

typedef struct {
    module_graph_issue_kind_t kind;
    size_t node, edge, other_edge; /**< SIZE_MAX for inapplicable indices. */
    const char *reference;         /**< Export name for REFERENCE; NULL otherwise. */
} module_graph_issue_t;

/** Validate the complete reachable graph with one fixed search path snapshot.
 * Each identity is read/validated at most once, including failed lookups and the
 * archive root. No recursion by dependency depth, code execution or OS loading.
 * Independent branches continue after failures. Edges retain all incoming paths;
 * hash conflicts never choose a different archive or suppress other requests.
 * On OK or INVALID, *out owns snapshots/diagnostics and survives all inputs.
 * INVALID is never safe to execute/publish. Argument/memory failure clears *out.
 * Root archive is read once and does not need to occupy its search-path location.
 * options callbacks observe unique archive checks, not repeated path diagnostics.
 */
module_graph_status_t validate_module_graph(const char *archive,
                                            const module_search_t *search,
                                            const module_validation_options_t *options,
                                            module_graph_t **out);
/** imports are the complete syntax-collected direct imports (including includes
 * and unreachable code). Defaults are already resolved, never inherited by a
 * library. Source root is synthetic; dependencies are not flattened in metadata.
 * This checks imports for analysis, not the eventual named build artifact. Before
 * publication, validate_module_graph must check the final temporary archive with
 * the original search object, including any edge back to the build's identity.
 */
module_graph_status_t validate_source_graph(const module_imports_t *imports,
                                            const module_search_t *search,
                                            const module_validation_options_t *options,
                                            module_graph_t **out);
void free_module_graph(module_graph_t *graph);
size_t module_graph_node_count(const module_graph_t *graph);
size_t module_graph_edge_count(const module_graph_t *graph);
size_t module_graph_issue_count(const module_graph_t *graph);
const module_graph_node_t *module_graph_node(const module_graph_t *graph, size_t index);
const module_graph_edge_t *module_graph_edge(const module_graph_t *graph, size_t index);
const module_graph_issue_t *module_graph_issue(const module_graph_t *graph, size_t index);

/** Called synchronously with root-to-target edge indices. Return false to stop
 * enumeration. Views are borrowed for the callback only; do not mutate graph.
 */
typedef bool (*module_graph_path_report_t)(const size_t *edges, size_t count, void *context);
/** Enumerate every simple root-to-node path iteratively, without storing an
 * exponential list. final_edge=SIZE_MAX selects node paths; otherwise enumerate
 * root-to-final_edge.from paths then append that edge (which must end at node).
 * This also exposes finite cycle witnesses with a repeated terminal node.
 * All conflict requests/expected hashes can be rendered through their edge paths.
 * Returns false only for invalid arguments or callback-requested termination.
 */
bool module_graph_paths(const module_graph_t *graph,
                        size_t node,
                        size_t final_edge,
                        module_graph_path_report_t report,
                        void *context);
