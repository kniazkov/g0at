#ifndef GOAT_MODULE_JSON_H
#define GOAT_MODULE_JSON_H

#include <stddef.h>
#include "yyjson.h"

/* Strict JSON reader for module descriptors. The caller owns the returned document. */
typedef enum goat_module_json_kind {
    GOAT_MODULE_GMOD,
    GOAT_MODULE_METADATA
} goat_module_json_kind;

typedef enum goat_module_json_error {
    GOAT_MODULE_JSON_OK,
    GOAT_MODULE_JSON_SYNTAX,
    GOAT_MODULE_JSON_DUPLICATE_KEY,
    GOAT_MODULE_JSON_UNKNOWN_FIELD,
    GOAT_MODULE_JSON_INVALID_FIELD,
    GOAT_MODULE_JSON_UNSUPPORTED_VERSION
} goat_module_json_error;

goat_module_json_error goat_module_json_read(const char *data, size_t length,
    goat_module_json_kind kind, yyjson_doc **out);
const char *goat_module_json_error_string(goat_module_json_error error);

#endif
