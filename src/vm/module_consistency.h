/** @file module_consistency.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Cross-part checks of owned, hash-verified module snapshots.
 */
#pragma once
#include "module/module_native.h"
#include "module_bytecode.h"

typedef enum {
    MODULE_CONSISTENCY_OK,
    MODULE_CONSISTENCY_ARGUMENT,
    MODULE_CONSISTENCY_JSON,
    MODULE_CONSISTENCY_BYTECODE,
    MODULE_CONSISTENCY_NATIVE,
    MODULE_CONSISTENCY_DESCRIPTOR,
    MODULE_CONSISTENCY_GRAPH,
    MODULE_CONSISTENCY_REFERENCE,
    MODULE_CONSISTENCY_MEMORY
} module_consistency_status_t;

typedef struct {
    module_consistency_status_t status;
    module_bytecode_error_t bytecode;
    goat_native_error native;
} module_consistency_error_t;

/** Validate every internal cross-link, independent of --native mode or host.
 * Input is a hash-verified container snapshot; no OS loading or execution.
 * On success returns a separate immutable bytecode image and main entry.
 * Failure clears both outputs. No dependency lookup or proof re-analysis.
 */
module_consistency_status_t read_module_consistency(const goat_module_container *container,
                                                    bytecode_t **out,
                                                    uint32_t *entry,
                                                    module_consistency_error_t *error);

/** Compare an already safely copied embedded descriptor with exactly one archive
 * variant. Identity, mainHash, target, ABI and specialization records must agree.
 * cCompiler/library/hash are external only. #210 owns mapped-range safety/loading.
 * Container must have passed read_module_consistency. No symbol lookup occurs.
 */
module_consistency_status_t match_module_descriptor(const goat_module_container *container,
                                                    size_t variant_index,
                                                    const void *snapshot,
                                                    size_t size);

/** Resolve all functionRef exports of owner through its transitive dependencies.
 * Each supplied container must have passed local consistency validation. Edges
 * resolve by exact name/version/mainHash, not global functionId or node position.
 * No filesystem lookup or initialization; unrelated nodes cannot satisfy refs.
 * Missing/ambiguous edges and duplicate module identities are errors. The caller
 * owns graph discovery/cycle policy (#189). This API only checks the supplied
 * snapshot and never substitutes closures or transfers access rights.
 */
module_consistency_status_t
check_module_references(const goat_module_container *const *nodes, size_t count, size_t owner);
