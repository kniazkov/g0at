/** @file module_compilation.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Compile an already resolved source graph without reopening its files.
 */
#pragma once
#include "module_sources.h"
#include "vm/bytecode.h"
#include "vm/module_dependencies.h"

typedef struct {
    bytecode_t *code;
    yyjson_doc *metadata;
    uint32_t *entries;
    size_t count;
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
/** @brief Release all compilation results; accepts NULL. */
void free_module_compilation(module_compilation_t *compilation);
