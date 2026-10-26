/** @file provider.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Legacy native entry invoking the shared host exit service.
 */
#include "goat/native_abi.h"

static uint32_t GOAT_NATIVE_CALL invoke(uint32_t version,
                                        const goat_native_context_v2_t *context,
                                        uint32_t count,
                                        const goat_native_value_v2_t *args,
                                        goat_native_value_v2_t *result) {
    (void)result;
    if (version != GOAT_NATIVE_ABI_VERSION)
        return GOAT_NATIVE_ABI_MISMATCH;
    if (count == 1 && args[0].value.integer == 257)
        return GOAT_NATIVE_HOST_EXCEPTION;
    if (count == 1 && args[0].value.integer == 258) {
        (void)goat_native_request_exit(context, 1, GOAT_NATIVE_I64, 256);
        *result = (goat_native_value_v2_t){.type = GOAT_NATIVE_I64, .value.integer = 0};
        return GOAT_NATIVE_OK;
    }
    return goat_native_request_exit(context,
                                    count,
                                    count ? args[0].type : 0,
                                    count ? args[0].value.integer : 0);
}

static const uint32_t arguments[] = {GOAT_NATIVE_I64};
static const goat_native_entry_v2_t entry = {0, 1, 1, GOAT_NATIVE_I64, arguments, invoke, NULL, 0};
static const goat_native_module_v2_t module = {GOAT_NATIVE_ABI_VERSION,
                                               sizeof(goat_native_module_v2_t),
                                               sizeof(goat_native_value_v2_t),
                                               _Alignof(goat_native_value_v2_t),
                                               sizeof(goat_native_entry_v2_t),
                                               sizeof(void *),
                                               1,
                                               &entry};

GOAT_NATIVE_API const goat_native_module_v2_t *GOAT_NATIVE_CALL
goat_native_query_v2(uint32_t version) {
    return version == GOAT_NATIVE_ABI_VERSION ? &module : NULL;
}
