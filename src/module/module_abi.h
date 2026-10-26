/** @file module_abi.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Bounded descriptor parsing and checked module-v1 adapter boundary.
 */
#pragma once
#include "module_json.h"

#include <goat/module_abi.h>
#include <stdbool.h>

typedef enum goat_module_abi_error {
    GOAT_ABI_OK,
    GOAT_ABI_ARGUMENT,
    GOAT_ABI_DESCRIPTOR,
    GOAT_ABI_UNSUPPORTED,
    GOAT_ABI_METADATA,
    GOAT_ABI_VALUE,
    GOAT_ABI_CONTEXT,
    GOAT_ABI_CALL_FAILED
} goat_module_abi_error;

/** Parse an owned/readable exact envelope snapshot, not an unchecked OS pointer.
 * All decoded JSON fields are validated before returning an owned document.
 * Unknown ABI is rejected, never reinterpreted using the current interface.
 * No DLL/SO is opened. #210 supplies mapped-region ownership checks before copying;
 * match_module_descriptor compares identity/target/specializations with the archive.
 */
goat_module_abi_error goat_module_descriptor_read(const void *bytes, size_t size, yyjson_doc **out);

/** Validate specialization declarations in one schema-validated variant/descriptor.
 * No library is loaded. read_module_consistency checks Function/FUNC cross-links.
 */
goat_module_abi_error goat_module_abi_validate(const yyjson_val *variant);

/** Verify a readable borrowed range before any string pointer is dereferenced.
 * The host must keep the range stable through validation and result consumption.
 * Returning true promises readable memory, not ownership or a sandbox.
 */
typedef bool (*goat_module_readable)(void *state, const void *pointer, size_t length);

bool goat_module_value_valid(const goat_module_value_v1 *value,
                             goat_module_readable readable,
                             void *state);
bool goat_module_context_valid(const goat_module_context_v1 *context);

/** Invoke exactly once; failures never cause VM fallback.
 * A HOST_EXCEPTION status is a language exception only when the model host has
 * recorded the matching rejected service request. This low-level validator still
 * reports CALL_FAILED; an unbacked/forged status is a terminal ABI violation.
 * Model module_native dispatch supplies guards and VM conversion. Arguments must already be the
 * formal slots. Result is zeroed before entry and cleared on every failure. native_status reports
 * the raw adapter status (including unknown codes), or UINT32_MAX before entry.
 * Result must not overlap arguments. Host service tables and the stated argument
 * array must be valid accessible objects supplied by the trusted caller.
 * Strings remain borrowed and must be copied by the caller before cleanup.
 */
goat_module_abi_error goat_module_abi_invoke(goat_module_adapter_v1 adapter,
                                             uint32_t expected_result,
                                             const goat_module_context_v1 *context,
                                             uint32_t count,
                                             const goat_module_value_v1 *arguments,
                                             goat_module_value_v1 *result,
                                             goat_module_readable readable,
                                             void *state,
                                             uint32_t *native_status);
