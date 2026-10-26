/** @file inspect_report.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Versioned nonexecuting inspector report serialization.
 */
#pragma once
#include "module/module_native.h"
#include "vm/module_graph.h"

typedef struct metadata_record {
    char *archive;
    yyjson_doc *doc;
    struct metadata_record *next;
} metadata_record_t;

/** All arguments are borrowed. graph/nodes/target may be NULL for an early
 * handled error. code/message describe that error; information_kind/text are
 * used only for help/version. Returns false on serialization/output failure. */
bool write_inspect_json(const char *input,
                        const goat_native_environment *target,
                        bool explicit_target,
                        const module_graph_t *graph,
                        const module_graph_node_t *const *nodes,
                        size_t count,
                        const metadata_record_t *metadata,
                        int exit_code,
                        const char *code,
                        const char *message,
                        const char *information_kind,
                        const char *information_text);
