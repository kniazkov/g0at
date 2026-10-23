#pragma once

#include "yyjson.h"

#include <stddef.h>

/* Strict UTF-8 reader for v1 descriptors, maximum nesting depth 256.
 * Validates the complete schema before normalization/defaults. Returned numbers
 * remain RAW decimal tokens (including -0), never doubles. The caller owns the
 * normalized document and frees it with yyjson_doc_free. On failure *out is NULL.
 * Does not validate bytecode, dependency graphs, analysis proofs or native ABI.
 */
typedef enum goat_module_json_kind { GOAT_MODULE_GMOD, GOAT_MODULE_METADATA } goat_module_json_kind;

typedef enum goat_module_json_error {
    GOAT_MODULE_JSON_OK,
    GOAT_MODULE_JSON_SYNTAX,
    GOAT_MODULE_JSON_DUPLICATE_KEY,
    GOAT_MODULE_JSON_UNKNOWN_FIELD,
    GOAT_MODULE_JSON_INVALID_FIELD,
    GOAT_MODULE_JSON_UNSUPPORTED_VERSION,
    GOAT_MODULE_JSON_LIMIT,
    GOAT_MODULE_JSON_MEMORY
} goat_module_json_error;

goat_module_json_error goat_module_json_read(const char *data,
                                             size_t length,
                                             goat_module_json_kind kind,
                                             yyjson_doc **out);
const char *goat_module_json_error_string(goat_module_json_error error);
