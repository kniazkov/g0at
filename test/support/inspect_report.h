/** @file inspect_report.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Test-only structural inspection JSON and published-schema checks.
 */
#pragma once
#include "yyjson.h"

#include <stdbool.h>
#include <stddef.h>

/** Strict UTF-8 JSON with duplicate keys rejected recursively (including escaped keys).
 * The caller owns the returned document. Floating-point JSON numbers are supported.
 */
yyjson_mut_doc *inspect_report_parse(const char *data, size_t size, char *error, size_t error_size);
/** Validate the schema subset used by published Goat inspection schemas. Local and
 * relative-file JSON pointers are resolved against document/directory. Patterns are
 * the six regular languages in the published schemas; unknown patterns fail closed.
 * This is an independent test oracle, not the production schema validator.
 */
bool inspect_report_schema(yyjson_mut_val *value,
                           yyjson_mut_val *schema,
                           yyjson_mut_val *document,
                           const char *directory,
                           char *error,
                           size_t error_size);
