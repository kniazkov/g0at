#pragma once

#include "module_json.h"

#include <stdint.h>

#define GOAT_JSON_MAX_DEPTH 256

bool goat_json_integer(const yyjson_val *value, int64_t *out);
/* SIZE_MAX denotes malformed UTF-8; embedded NUL counts as one scalar. */
size_t goat_json_scalar_count(const char *text, size_t length);
goat_module_json_error goat_module_json_normalize(yyjson_mut_doc *doc, goat_module_json_kind kind);
