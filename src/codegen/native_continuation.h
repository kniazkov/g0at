/** @file native_continuation.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Compiled control-flow continuations for dynamically dispatched operations.
 */
#pragma once
#include "c_module_output.h"
#include "goat/native_continuation.h"
#include "vm/bytecode.h"

typedef struct {
    string_value_t source;
    size_t count;
    goat_native_continuation_entry_t *entries; /**< Owned expected identities; invoke is NULL. */
} native_continuation_output_t;

/** Borrowed AST is optional. Bytecode function identities and ranges are authoritative.
 * Scalar-only functions keep their existing specialized path or backend-limit fallback.
 */
native_continuation_output_t generate_native_continuations(const node_t *root,
                                                           const bytecode_t *code,
                                                           const c_module_t *module,
                                                           const c_module_output_t *scalar);
void destroy_native_continuation_output(native_continuation_output_t *output);
