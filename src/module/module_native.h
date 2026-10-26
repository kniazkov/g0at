/** @file module_native.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Native platform metadata and selection before any OS library loading.
 */
#pragma once
#include "module_container.h"
#include "native_environment.h"

#include <stdint.h>

typedef enum {
    GOAT_NATIVE_OK,
    GOAT_NATIVE_ARGUMENT,
    GOAT_NATIVE_INVALID_METADATA,
    GOAT_NATIVE_NO_MATCH,
    GOAT_NATIVE_AMBIGUOUS
} goat_native_status;

typedef struct {
    goat_native_status status;
    size_t variant_index; /* SIZE_MAX if not associated with a variant. */
} goat_native_error;

/** Parse a strict target.schema.json profile. Version fields describe available
 * versions here. Unknown CPU names confer no capability; known names require
 * valid architectural placement. A profile can model missing capabilities;
 * selection requires the complete prerequisite set. Clears *out on failure.
 */
goat_module_json_error
goat_native_environment_read(const char *data, size_t size, goat_native_environment *out);

/** Select exactly one compatible variant in an already hash-verified container.
 * All target records are validated, including incompatible variants. Unknown
 * requirements/ABI versions exclude variants; two matches are an error, with no
 * preference ordering. ABI versions must name interfaces the caller implements.
 * No OS loading, code execution or mutation. The result is an index in the
 * normalized metadata.native array, not the ZIP central directory.
 * Failure/no match clears *selected_index to SIZE_MAX. An empty ABI list allows
 * no native variant. Does not implement native mode/VM fallback policy.
 */
goat_native_status goat_module_native_select(const goat_module_container *container,
                                             const goat_native_environment *environment,
                                             const uint32_t *supported_abis,
                                             size_t abi_count,
                                             size_t *selected_index,
                                             goat_native_error *error);
const char *goat_native_status_string(goat_native_status status);

/** Check every target, library suffix and specialization declaration without
 * selecting a host or loading code. Unknown ABI/CPU requirements are permitted
 * incompatibilities, not malformed metadata. Container hashes are already checked.
 */
goat_native_status goat_module_native_validate(const goat_module_container *container,
                                               goat_native_error *error);

/** Called once for each malformed variant, in normalized metadata array order.
 * SIZE_MAX identifies an invalid API argument. No callbacks denote success.
 */
typedef void (*goat_native_diagnostic)(goat_native_status status,
                                       size_t variant_index,
                                       void *context);
/** Validate all independent native records and report failures. The returned
 * status/error identify the first failure, preserving validate's contract.
 * Does not select or load a native library, even after a successful record.
 */
goat_native_status goat_module_native_validate_diagnostics(const goat_module_container *container,
                                                           goat_native_error *error,
                                                           goat_native_diagnostic callback,
                                                           void *context);
