/** @file module_bytecode.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Portable bytecode validation against v1 module metadata.
 */
#pragma once

#include "bytecode.h"
#include "module/module_json.h"

/** @brief Validation stage; no execution, native loading or filesystem access occurs. */
typedef enum {
    MODULE_BYTECODE_OK,
    MODULE_BYTECODE_ARGUMENT,
    MODULE_BYTECODE_JSON,
    MODULE_BYTECODE_IMAGE,
    MODULE_BYTECODE_FILES,
    MODULE_BYTECODE_FUNCTIONS,
    MODULE_BYTECODE_PARAMETERS,
    MODULE_BYTECODE_IMPORTS
} module_bytecode_status_t;

/** @brief First failing stage, with an optional table/instruction index. */
typedef struct {
    module_bytecode_status_t status;
    size_t index; /**< SIZE_MAX when no individual record is implicated. */
    goat_module_json_error json_error;
} module_bytecode_error_t;

/** @brief Validate untrusted JSON and image, then return an owned immutable image.
 * On success, *entry is the instruction selected through metadata.main/files.
 * All instructions, including unreachable ones, are checked. Both inputs are
 * borrowed only during this call; *out is released with free_bytecode(). Failure
 * clears *out and *entry; error is optional. out and entry are required.
 * Hash/container checks and dependency-graph/native/analysis validation belong
 * to their respective layers, not this API. Allocation failure follows the
 * core allocator's fatal policy, never a successful or partial load.
 */
module_bytecode_status_t read_module_bytecode(const char *metadata,
                                              size_t metadata_size,
                                              const void *image,
                                              size_t image_size,
                                              bytecode_t **out,
                                              uint32_t *entry,
                                              module_bytecode_error_t *error);
const char *module_bytecode_status_string(module_bytecode_status_t status);
