/** @file native_artifact_validation.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Validate produced descriptor and adapter exports without loading executable code.
 */
#pragma once
#include "module/module_json.h"

/** @brief Metadata has passed its schema. Check exactly one variant against a native image.
 * Opens a bounded file snapshot; validates the actual exported descriptor and every declared
 * adapter function. No execution or OS loading. Failure returns owned diagnostic text.
 */
bool validate_native_artifact(const char *path,
                              const yyjson_doc *metadata,
                              size_t variant_index,
                              char **diagnostic);

/** @brief Same checks on the exact stored byte snapshot; no path is reopened. */
bool validate_native_artifact_bytes(const void *bytes,
                                    size_t size,
                                    const yyjson_doc *metadata,
                                    size_t variant_index,
                                    char **diagnostic);
