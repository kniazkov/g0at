#pragma once

#include "module_json.h"

#define GOAT_MODULE_HASH_HEX_SIZE 65

/** Validate/normalize metadata and hash its v1 projection plus exact bytecode.
 * Input is borrowed and unchanged. bytecode may be NULL only for zero length.
 * Output is 64 lowercase hex digits and NUL; errors clear all 65 bytes.
 * mainHash must be schema-valid (64 zeros can be used while constructing a module).
 * No bytecode semantics, archive, dependency graph or native ABI validation.
 */
goat_module_json_error goat_module_main_hash(const yyjson_doc *metadata,
                                             const void *bytecode,
                                             size_t length,
                                             char out[GOAT_MODULE_HASH_HEX_SIZE]);

/** Compare the calculated digest with metadata.mainHash. A mismatch returns OK
 * with *matches=false; malformed input returns a JSON error and false.
 */
goat_module_json_error goat_module_main_hash_verify(const yyjson_doc *metadata,
                                                    const void *bytecode,
                                                    size_t length,
                                                    bool *matches);
