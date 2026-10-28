/** @file format_metadata.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent format metadata oracle, for tests only.
 */
#pragma once
#include "format_json.h"

/** Normalize in place after independent shape validation against schema-source.json. */
bool format_metadata_normalize(yyjson_mut_doc *doc,
                               yyjson_mut_val *value,
                               const char *kind,
                               yyjson_mut_val *source,
                               char *error,
                               size_t error_size);
/** Guards must already satisfy shape/scalar validation, as in reference.py. */
bool format_metadata_guards_overlap(yyjson_mut_val *a, yyjson_mut_val *b);
/** Returned buffers are malloc-owned. Inputs are never modified. */
char *format_metadata_main_bytes(yyjson_mut_val *metadata,
                                 yyjson_mut_val *source,
                                 size_t *size,
                                 char *error,
                                 size_t error_size);
unsigned char *format_metadata_hash_input(yyjson_mut_val *metadata,
                                          yyjson_mut_val *source,
                                          const unsigned char *bytecode,
                                          size_t bytecode_size,
                                          size_t *size,
                                          char *error,
                                          size_t error_size);
