/** @file module_graph_diagnostics.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Deterministically ordered, nonexecuting dependency diagnostics.
 */
#pragma once
#include "module_graph.h"

/** Borrowed synchronous view. path contains root-to-terminal edge indices;
 * validation is NULL for graph/search errors and retained for failed/skipped
 * archive checks. edge is the terminal edge (SIZE_MAX for the root). */
typedef struct {
    module_graph_issue_kind_t kind;
    size_t node, edge;
    const size_t *path;
    size_t path_count;
    const char *reference;
    size_t reference_length;
    const module_validation_diagnostic_t *validation;
} module_graph_diagnostic_t;

typedef bool (*module_graph_diagnostic_report_t)(const module_graph_diagnostic_t *, void *);

/** Report every full path to each error, sorted by name/version chain (unsigned
 * UTF-8 bytes, shorter prefix first), kind, validation check/detail/cause, then location
 * (entry, field, index, reference). Expected hashes and remaining structured
 * details break ties, never discovery indices or callback arrival order.
 * Conflicts report every incoming request, not just the first conflicting pair.
 * Cycles use finite simple paths with an appended closing edge. No archive reads,
 * execution, graph mutation, or recursion by dependency depth. Reporting explicitly
 * materializes its output paths; their number can be exponential. Validation
 * itself keeps only graph adjacency. Callback false cancels delivery and frees
 * temporary rows; false also means invalid arguments. A graph without errors succeeds.
 */
bool report_module_graph_diagnostics(const module_graph_t *graph,
                                     module_graph_diagnostic_report_t report,
                                     void *context);
/** Owned English UTF-8 text (FREE). Empty for a valid graph, NULL for NULL graph.
 * Untrusted labels/fields/paths are quoted and control bytes escaped. */
char *format_module_graph_diagnostics(const module_graph_t *graph);
