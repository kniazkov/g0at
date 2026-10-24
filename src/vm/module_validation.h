/** @file module_validation.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared nonexecuting validation and owned snapshots of one .gbin.
 */
#pragma once
#include "module_consistency.h"

typedef enum {
    MODULE_VALIDATION_OK,
    MODULE_VALIDATION_ARGUMENT,
    MODULE_VALIDATION_IO,
    MODULE_VALIDATION_LIMIT,
    MODULE_VALIDATION_MEMORY,
    MODULE_VALIDATION_CORRUPT
} module_validation_status_t;

typedef enum {
    MODULE_CHECK_INPUT,
    MODULE_CHECK_CONTAINER,
    MODULE_CHECK_BYTECODE,
    MODULE_CHECK_NATIVE
} module_validation_check_t;

typedef enum {
    MODULE_CHECK_PASSED,
    MODULE_CHECK_FAILED,
    MODULE_CHECK_SKIPPED
} module_check_state_t;

/** Borrowed callback data, valid only during the callback. archive is the caller's
 * label/path, entry is a bounded ZIP name, field is a JSON pointer. prerequisite
 * explains skipped checks structurally; detail carries the original lower-level
 * error without parsing message text. Order is deterministic, never parallel.
 */
typedef struct {
    module_validation_check_t check;
    module_check_state_t state;
    module_validation_check_t prerequisite;
    const char *archive;
    zip_view_t entry;
    const char *field;
    size_t field_length;
    goat_container_check container_prerequisite;
    int detail;
    size_t index;
    int system_error;
    goat_container_error container;
    module_bytecode_error_t bytecode;
    goat_native_error native;
} module_validation_diagnostic_t;

typedef void (*module_validation_report_t)(const module_validation_diagnostic_t *diagnostic,
                                           void *context);

typedef struct {
    size_t max_archive,
        max_expanded; /**< SIZE_MAX permits the implementation/address-space limit. */
    module_validation_report_t report; /**< Optional synchronous report; must not mutate inputs. */
    void *context;
} module_validation_options_t;

typedef struct {
    module_validation_status_t status;
    size_t failures, skipped;
} module_validation_result_t;
typedef struct validated_module validated_module_t;

/** Validate exactly one archive. Inputs are borrowed only for this call. On
 * success *out owns all metadata, expanded libraries and immutable bytecode;
 * failure always clears *out. No OS loading, extraction, execution, dependency
 * lookup or semantic re-analysis occurs, in any native mode. Native mode is
 * deliberately not an argument. NULL options use SIZE_MAX limits/no report.
 * Callbacks may observe independent errors; skipped checks are never successes.
 * Existing bytecode allocation uses the core fatal-OOM policy (#236).
 */
module_validation_result_t validate_module(const void *data,
                                           size_t size,
                                           const char *archive,
                                           const module_validation_options_t *options,
                                           validated_module_t **out);
/** Read once through the shared UTF-8 filesystem layer, then validate the owned
 * bytes. The returned snapshot never reopens the path. I/O errors retain errno.
 */
module_validation_result_t validate_module_file(const char *path,
                                                const module_validation_options_t *options,
                                                validated_module_t **out);
void free_validated_module(validated_module_t *module);
/** All returned views are borrowed, immutable, and valid until free_validated_module. */
const goat_module_container *validated_module_container(const validated_module_t *module);
const bytecode_t *validated_module_bytecode(const validated_module_t *module);
const char *validated_module_archive(const validated_module_t *module);
uint32_t validated_module_entry(const validated_module_t *module);
