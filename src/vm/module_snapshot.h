/** @file module_snapshot.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Process-lifetime, fully preflighted in-memory module graph.
 */
#pragma once
#include "model/module_files.h"
#include "module_graph.h"

typedef struct module_snapshot module_snapshot_t;

/** Read and validate the entire reachable graph before constructing runtime
 * scopes. Success owns one process, all module scopes, immutable images, metadata
 * and native payloads. Nothing is executed, extracted or OS-loaded here.
 * Inputs/search/options are borrowed only during this call. Neither later lookup
 * nor execution needs the source archives. Out is cleared on every failure;
 * optional diagnostics receives the owned INVALID graph (otherwise discarded).
 * Core runtime allocation retains its fatal-OOM policy. A snapshot is not
 * published partially. NULL/argument/memory failures have no diagnostic graph.
 */
module_graph_status_t load_module_snapshot(const char *archive,
                                           const module_search_t *search,
                                           const module_validation_options_t *options,
                                           module_snapshot_t **out,
                                           module_graph_t **diagnostics);
/** Call only after all execution/threads/callbacks have stopped. Frees managed
 * objects and runtime wrappers BEFORE the borrowed immutable images. No module
 * can be unloaded individually. NULL is allowed.
 */
void free_module_snapshot(module_snapshot_t *snapshot);
/** Borrowed views last until free_module_snapshot. Do not destroy the process,
 * file sets or validated modules separately. Node zero is the root; modules
 * not reachable from that root are absent. Invalid handles/indices yield NULL/0.
 */
process_t *module_snapshot_process(module_snapshot_t *snapshot);
size_t module_snapshot_count(const module_snapshot_t *snapshot);
const module_graph_t *module_snapshot_graph(const module_snapshot_t *snapshot);
const validated_module_t *module_snapshot_module(const module_snapshot_t *snapshot, size_t node);
module_files_t *module_snapshot_files(module_snapshot_t *snapshot, size_t node);
/** Exact immutable build identity within this launch. Physical archive paths,
 * informational metadata and native variants are not identity components.
 * Returns SIZE_MAX for absent/invalid arguments or any mismatch (including hash).
 * This is an internal inventory lookup, NOT an import or access grant. Language
 * imports must use the consumer-local bindings below. No I/O or new instance is
 * created; repeated lookup returns the same node, scopes and access identity.
 */
size_t module_snapshot_find(const module_snapshot_t *snapshot,
                            const char *name,
                            const char *version,
                            const char *hash);
/** Consumer-local immediate bindings, never a global/transitive search. All
 * bindings are fixed during preparation, pinning the verified target/hash.
 * SIZE_MAX means absent/invalid. No filesystem access or fallback is performed.
 * Both name and exact version are required; .gmod defaults belong to compilation.
 */
size_t module_snapshot_dependency_count(const module_snapshot_t *snapshot, size_t consumer);
size_t
module_snapshot_dependency(const module_snapshot_t *snapshot, size_t consumer, size_t dependency);
size_t module_snapshot_import(const module_snapshot_t *snapshot,
                              size_t consumer,
                              const char *name,
                              const char *version);
