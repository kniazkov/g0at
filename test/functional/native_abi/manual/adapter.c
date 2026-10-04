/** @file adapter.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Handwritten adapter for a separately built vendor archive.
 */
#include "vendor.h"

#include <goat/native_abi.h>

static uint32_t GOAT_NATIVE_CALL read_device(uint32_t version,
                                             uint32_t count,
                                             const goat_native_value_v1_t *args,
                                             goat_native_value_v1_t *result) {
    if (version != GOAT_NATIVE_ABI_VERSION)
        return GOAT_NATIVE_ABI_MISMATCH;
    if (!result || (count && !args))
        return GOAT_NATIVE_BAD_REQUEST;
    if (count < 1)
        return GOAT_NATIVE_TYPE_MISMATCH;
    if (args[0].reserved)
        return GOAT_NATIVE_BAD_REQUEST;
    if (args[0].type != GOAT_NATIVE_I64)
        return GOAT_NATIVE_TYPE_MISMATCH;
    goat_native_value_v1_t value = {.type = GOAT_NATIVE_F64};
    if (vendor_read(args[0].value.integer, &value.value.real))
        return GOAT_NATIVE_EXTERNAL_ERROR;
    *result = value;
    return GOAT_NATIVE_OK;
}

static const uint32_t parameters[] = {GOAT_NATIVE_I64};
static const goat_native_entry_v1_t entries[] = {{
    .specialization_id = 17,
    .function_id = 9,
    .parameter_count = 1,
    .return_type = GOAT_NATIVE_F64,
    .parameter_types = parameters,
    .invoke = read_device,
    .binding_name = "device.read",
    .flags = 0,
}};
static const goat_native_module_v1_t module = {
    .abi_version = GOAT_NATIVE_ABI_VERSION,
    .struct_size = sizeof(goat_native_module_v1_t),
    .value_size = sizeof(goat_native_value_v1_t),
    .value_alignment = _Alignof(goat_native_value_v1_t),
    .entry_size = sizeof(goat_native_entry_v1_t),
    .pointer_size = sizeof(void *),
    .entry_count = 1,
    .entries = entries,
};

GOAT_NATIVE_API const goat_native_module_v1_t *GOAT_NATIVE_CALL
goat_native_query_v1(uint32_t version) {
    return version == GOAT_NATIVE_ABI_VERSION ? &module : 0;
}
