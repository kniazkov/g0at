/** @file module_writer.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Deterministic, validated module-v1 packaging and atomic publication.
 */
#pragma once
#include "lib/files/file_system.h"
#include "module_validation.h"

typedef enum {
    MODULE_WRITER_OK,
    MODULE_WRITER_ARGUMENT,
    MODULE_WRITER_JSON,
    MODULE_WRITER_PARTS,
    MODULE_WRITER_LIMIT,
    MODULE_WRITER_ZIP,
    MODULE_WRITER_VALIDATION,
    MODULE_WRITER_DESTINATION,
    MODULE_WRITER_IO,
    MODULE_WRITER_MEMORY
} module_writer_status_t;

typedef struct {
    const char *name;
    size_t name_length;
    zip_view_t bytes;
} module_writer_native_t;

typedef struct {
    const yyjson_doc *metadata;
    zip_view_t bytecode;
    const module_writer_native_t *native;
    size_t native_count;
} module_writer_input_t;

typedef struct {
    size_t max_archive, max_expanded;
} module_writer_options_t;

/** Initialize with {0}; clear before releasing. The first failure is retained;
 * cleanup separately reports failure to remove the unpublished temporary file.
 */
typedef struct {
    module_writer_status_t status;
    goat_module_json_error json;
    zip_status_t zip;
    module_validation_result_t validation;
    fs_error_t filesystem, cleanup;
} module_writer_error_t;

void clear_module_writer_error(module_writer_error_t *error);

/** Borrow inputs only during the call; never modify metadata, images or libraries.
 * Require valid supplied main/native hashes, not placeholders. Match each native
 * payload exactly once by its ORIGINAL metadata library name. Normalize full JSON,
 * assign native/variant-N.dll or .so in normalized variant order, then ZIP profile 1
 * with every entry Deflated. No dependency lookup, execution or library loading.
 * Fully validate output through validate_module before exposing it. NULL options
 * use SIZE_MAX limits. Failure clears output and size. Free output below.
 */
module_writer_status_t module_write_memory(const module_writer_input_t *input,
                                           const module_writer_options_t *options,
                                           void **out,
                                           size_t *size,
                                           module_writer_error_t *error);
void free_module_writer_bytes(void *bytes);

/** Write a unique sibling temporary file, check write/close, validate its actual
 * contents through the shared API, preserve destination permissions, then atomic
 * rename only. Reject a final symlink or non-regular destination, including a
 * detected substitution before publication. Parent directories must exist.
 * Caller serializes destination mutations; this is not protection against an
 * adversarial race after the final check. No delete/copy fallback, extension
 * rewriting or power-loss durability promise. Errors before rename preserve the
 * previous destination. Destination is never implicitly read as an input module.
 */
module_writer_status_t module_write_file(const char *path,
                                         const module_writer_input_t *input,
                                         const module_writer_options_t *options,
                                         module_writer_error_t *error);
