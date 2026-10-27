/** @file module_thread_native_fixture.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Trusted native entry first invoked after the entry thread has completed.
 */
#define GOAT_NATIVE_BUILD
#include <goat/native_abi.h>

static uint32_t GOAT_NATIVE_CALL invoke(uint32_t version,
                                        const goat_native_context_v2_t *context,
                                        uint32_t count,
                                        const goat_native_value_v2_t *args,
                                        goat_native_value_v2_t *result) {
    (void)context;
    (void)args;
    if (version != GOAT_NATIVE_ABI_VERSION || count)
        return GOAT_NATIVE_BAD_REQUEST;
    *result = (goat_native_value_v2_t){.type = GOAT_NATIVE_I64, .value.integer = 42};
    return GOAT_NATIVE_OK;
}

static const goat_native_entry_v2_t entry =
    {0, 1, 0, GOAT_NATIVE_I64, NULL, invoke, NULL, GOAT_NATIVE_PURE};
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
