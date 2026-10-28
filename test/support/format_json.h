/** @file format_json.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent JSON format oracle; never calls production validators.
 */
#pragma once

#include "yyjson.h"

#include <stdbool.h>
#include <stddef.h>

/** Strict JSON, retaining arbitrary integer tokens without a floating-point conversion.
 * The returned mutable document owns all values. NULL reports invalid input.
 */
yyjson_mut_doc *format_json_parse(const char *data, size_t size, char *error, size_t error_size);
/** Canonical UTF-8 bytes; caller frees the result. size excludes the terminating NUL. */
char *format_json_canonical(yyjson_mut_val *value, size_t *size);
bool format_json_equal(yyjson_mut_val *a, yyjson_mut_val *b);
/** Validate the subset used by schema-source.json, resolving references in defs. */
bool format_json_shape(yyjson_mut_val *value,
                       yyjson_mut_val *schema,
                       yyjson_mut_val *defs,
                       char *error,
                       size_t error_size);
