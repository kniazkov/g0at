/** @file format_analysis.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent test-only normalization of analysis metadata.
 */
#pragma once

#include "test/support/format_json.h"

bool format_scalar(const char *kind, yyjson_mut_val *value, char *error, size_t error_size);

/** Normalize a facts object in place, using the agreed schema definitions. */
bool format_facts(yyjson_mut_doc *doc,
                  yyjson_mut_val *value,
                  yyjson_mut_val *defs,
                  char *error,
                  size_t error_size);

/** Inputs must be normalized facts. NULL represents unconstrained facts. */
bool format_facts_intersect(yyjson_mut_val *a, yyjson_mut_val *b);

/** Normalize in place. An analysis with no informative summaries becomes null. */
bool format_analysis(yyjson_mut_doc *doc,
                     yyjson_mut_val *value,
                     size_t arity,
                     yyjson_mut_val *defs,
                     char *error,
                     size_t error_size);
