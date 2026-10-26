/** @file module_compilation.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Compile an already resolved source graph without reopening its files.
 */
#pragma once
#include "graph/node.h"
#include "module_sources.h"
#include "vm/bytecode.h"
#include "vm/module_dependencies.h"
#include "vm/module_graph.h"

/** Nonfatal static-call diagnostic. Message and storage belong to the compilation. */
typedef struct {
    size_t file, row, column;
    char *message;
} module_compilation_warning_t;

typedef struct {
    bytecode_t *code;
    yyjson_doc *metadata;
    uint32_t *entries;
    size_t count;
    module_compilation_warning_t *warnings;
    size_t warning_count;
} module_compilation_t;

/** Initialize with {0}; clear after use. Owns its UTF-8 diagnostic message. */
typedef struct {
    size_t file, row, column;
    char *message;
} module_compilation_error_t;

/** @brief Release the saved diagnostic; accepts NULL. */
void clear_module_compilation_error(module_compilation_error_t *error);
/** Borrow immutable source snapshots. Output owns checked bytecode, normalized
 * metadata (including main hash), and file entries. No filesystem/runtime I/O.
 * Module optimization is conservative: exported bindings are never eliminated.
 * This convenience wrapper accepts no imports; use the dependency-aware API
 * below whenever the source graph contains import expressions.
 */
bool compile_module_sources(const module_sources_t *sources,
                            module_compilation_t **out,
                            module_compilation_error_t *error);
/** Borrow source snapshots and their exact direct dependency bindings. Every
 * syntactic import (including unreachable code) must match a binding, with no
 * extra/transitive bindings. Metadata records the validated name/version/hash.
 * No dependency code is executed. The caller must preflight the complete graph
 * before publishing or executing the result. NULL bindings allow no imports.
 */
bool compile_module_sources_with_dependencies(const module_sources_t *sources,
                                              const module_dependencies_t *dependencies,
                                              module_compilation_t **out,
                                              module_compilation_error_t *error);
/** As above, with a successfully validated source graph for transitive functionRef
 * resolution. The graph must pin exactly the supplied direct snapshots. All
 * metadata is borrowed for analysis only; no dependency code is analyzed or run.
 * NULL graph retains direct metadata facts but cannot resolve transitive references.
 */
bool compile_module_sources_with_graph(const module_sources_t *sources,
                                       const module_dependencies_t *dependencies,
                                       const module_graph_t *graph,
                                       module_compilation_t **out,
                                       module_compilation_error_t *error);
/** @brief Release all compilation results; accepts NULL. */
void free_module_compilation(module_compilation_t *compilation);

/** Optional backend runs only after verified immutable bytecode/metadata are complete,
 * while source analysis graphs are still alive. Never retain graph pointers.
 * Returning false aborts the transaction and releases the compilation.
 */
typedef bool (*module_compilation_backend_t)(const node_t *const *roots,
                                             size_t count,
                                             module_compilation_t *compilation,
                                             void *context,
                                             module_compilation_error_t *error);
bool compile_module_sources_with_backend(const module_sources_t *sources,
                                         const module_dependencies_t *dependencies,
                                         const module_graph_t *graph,
                                         module_compilation_backend_t backend,
                                         void *context,
                                         module_compilation_t **out,
                                         module_compilation_error_t *error);
