/** @file module_imports.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Syntax-only direct dependency collection from owned source snapshots.
 */
#pragma once
#include "module_sources.h"

/** Source occurrence, retained even for repeated imports and unreachable code.
 * file is an index into the source graph, never a machine-dependent path.
 */
typedef struct {
    size_t file, row, column, offset;
} module_import_site_t;

/** Exact name/version pair and all occurrences in file-index/source order. */
typedef struct {
    char *name, *version;
    module_import_site_t *sites;
    size_t count;
} module_import_dependency_t;

/** Owned result: dependencies and unused defaults sorted by name then version.
 * Unused defaults are warnings, not dependencies, and have no source sites.
 */
typedef struct {
    module_import_dependency_t *dependencies, *unused_defaults;
    size_t count, unused_count;
} module_imports_t;

typedef enum {
    MODULE_IMPORTS_OK,
    MODULE_IMPORTS_ARGUMENT,
    MODULE_IMPORTS_DESCRIPTOR,
    MODULE_IMPORTS_SYNTAX,
    MODULE_IMPORTS_IDENTITY,
    MODULE_IMPORTS_VERSION,
    MODULE_IMPORTS_LIMIT
} module_imports_status_t;

typedef struct {
    module_imports_status_t status;
    module_import_site_t site;
    const char *message;
    goat_module_json_error json;
} module_imports_error_t;

/** Collect all literal imports from all files, independently of execution or
 * optimization. Explicit versions override defaults only for that occurrence.
 * A direct-source graph has no defaults. No filesystem access, module search,
 * code execution, hash guessing or transitive dependency copying occurs.
 * max_imports bounds occurrences (zero selects 65536). Input remains unchanged;
 * result owns strings/sites and survives freeing the source graph. Failure sets
 * *out to NULL; an unused default yields one warning, never an error. An explicit
 * import of the default pair counts as use; importing another version does not.
 * Full descriptor validation precedes default lookup, even for manually built
 * graphs. Import sites refer to input file indices and decoded Unicode offsets.
 */
module_imports_status_t module_imports_collect(const module_sources_t *sources,
                                               size_t max_imports,
                                               module_imports_t **out,
                                               module_imports_error_t *error);
void free_module_imports(module_imports_t *imports);
