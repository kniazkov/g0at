#pragma once

#include "yyjson.h"

#include <stddef.h>

/* Strict UTF-8 reader for v1 descriptors, maximum nesting depth 256.
 * Validates the complete schema before normalization/defaults. Returned numbers
 * remain RAW decimal tokens (including -0), never doubles. The caller owns the
 * normalized document and frees it with yyjson_doc_free. On failure *out is NULL.
 * Does not validate bytecode, dependency graphs, analysis proofs or native ABI.
 */
typedef enum goat_module_json_kind {
    GOAT_MODULE_GMOD,
    GOAT_MODULE_METADATA,
    GOAT_MODULE_TARGET,
    GOAT_MODULE_NATIVE
} goat_module_json_kind;

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
/** Diagnostic paths are RFC 6901 JSON pointers, root is empty. The path is
 * borrowed only during the callback and length-delimited (keys may contain NUL).
 * Independent schema errors are reported in document order, followed by missing
 * required fields in schema order. Malformed syntax/duplicate keys prevent schema
 * checks; record invariants and normalization run only after the schema succeeds.
 */
typedef void (*goat_module_json_diagnostic)(goat_module_json_error error,
                                            const char *path,
                                            size_t path_length,
                                            void *context);
/** Same ownership/result contract as read. On errors no partial document escapes.
 * Returns the first schema error (allocation failure takes precedence).
 */
goat_module_json_error goat_module_json_read_diagnostics(const char *data,
                                                         size_t length,
                                                         goat_module_json_kind kind,
                                                         yyjson_doc **out,
                                                         goat_module_json_diagnostic callback,
                                                         void *context);
const char *goat_module_json_error_string(goat_module_json_error error);

/** Canonical JSON syntax only: UTF-8 key order, exact int64, no array reordering.
 * Rejects duplicate keys, malformed UTF-8, real numbers and non-integer RAW.
 * Input is borrowed and unchanged. Free successful output with the function below.
 * Length excludes the convenience trailing NUL. Failure clears output and length.
 */
goat_module_json_error goat_json_canonical(const yyjson_val *value, char **out, size_t *length);

/** Validate and normalize a descriptor, then serialize canonical bytes.
 * main_metadata selects exactly the v1 main-hash fields (metadata kind only).
 * This does not calculate a hash or validate bytecode/native ABI.
 */
goat_module_json_error goat_module_json_write(const yyjson_doc *doc,
                                              goat_module_json_kind kind,
                                              bool main_metadata,
                                              char **out,
                                              size_t *length);
/** Release canonical output using the allocator of the module library. */
void goat_module_json_free(char *data);

/** Validate a complete .gmod or module.json and return an owned information-only
 * object (description, license, homepage, authors, and metadata-only producers).
 * Empty values are omitted, author order is retained and producers are sorted.
 * Strings are decoded UTF-8 with explicit lengths: embedded NUL is data, not a
 * terminator. No trimming, Unicode normalization, URL/email parsing or network
 * access occurs. A successful empty projection is {}, never NULL. Input remains
 * unchanged; free the independent result with yyjson_doc_free. Failure clears out.
 * This is not a hash projection, producer generator or merge-conflict resolver.
 */
goat_module_json_error
goat_module_information(const yyjson_doc *descriptor, goat_module_json_kind kind, yyjson_doc **out);
