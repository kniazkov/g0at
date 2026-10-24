/** @file module_dependencies.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Bind one consumer's direct dependencies without version overrides.
 */
#pragma once
#include "common/module_imports.h"
#include "module_search.h"

typedef enum {
    MODULE_DEPENDENCIES_OK,
    MODULE_DEPENDENCIES_ARGUMENT,
    MODULE_DEPENDENCIES_SEARCH,
    MODULE_DEPENDENCIES_MEMORY
} module_dependencies_status_t;

typedef struct module_dependencies module_dependencies_t;

/** Initialize with zero and clear after use. index is the canonical direct-edge
 * index on search failure (SIZE_MAX otherwise); search retains the exact cause.
 */
typedef struct {
    module_dependencies_status_t status;
    size_t index;
    module_search_error_t search;
} module_dependencies_error_t;

/** Bind only collected direct imports. Unused defaults are never searched.
 * Each resulting record gets the fully validated dependency's actual mainHash.
 * Inputs are borrowed unchanged. No transitive entries or payloads are embedded.
 * Manually constructed imports must contain valid, unique exact pairs; ordering
 * is immaterial. Source sites and unused-default warnings remain with imports.
 */
module_dependencies_status_t bind_source_dependencies(const module_imports_t *imports,
                                                      const module_search_t *search,
                                                      const module_validation_options_t *options,
                                                      module_dependencies_t **out,
                                                      module_dependencies_error_t *error);
/** Bind only a validated library's own metadata records, enforcing each stored
 * name/version/hash. No application descriptor/defaults parameter exists: callers
 * cannot override these edges. Reuse the root program's search object everywhere.
 * Neither API traverses the graph, detects cycles, caches nodes, or executes code;
 * whole-graph validation is a separate layer and MUST precede execution/publication.
 */
module_dependencies_status_t bind_module_dependencies(const validated_module_t *consumer,
                                                      const module_search_t *search,
                                                      const module_validation_options_t *options,
                                                      module_dependencies_t **out,
                                                      module_dependencies_error_t *error);
/** Result owns snapshots and canonical (name,version sorted) metadata array
 * [{name,version,hash}], surviving input/search destruction. Failure clears *out
 * and frees every partial result. NULL error is allowed. Borrowed views below
 * remain valid until free; invalid handles/indices return NULL or zero.
 */
void free_module_dependencies(module_dependencies_t *dependencies);
void clear_module_dependencies_error(module_dependencies_error_t *error);
size_t module_dependencies_count(const module_dependencies_t *dependencies);
const validated_module_t *module_dependency_snapshot(const module_dependencies_t *dependencies,
                                                     size_t index);
const yyjson_doc *module_dependencies_metadata(const module_dependencies_t *dependencies);
