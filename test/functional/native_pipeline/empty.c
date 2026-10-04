/** @file empty.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Valid ABI with deliberately unrelated contents.
 */
#include <goat/native_abi.h>
#include <stddef.h>
static const goat_native_module_v1_t module = {GOAT_NATIVE_ABI_VERSION,
                                               sizeof(goat_native_module_v1_t),
                                               sizeof(goat_native_value_v1_t),
                                               _Alignof(goat_native_value_v1_t),
                                               sizeof(goat_native_entry_v1_t),
                                               sizeof(void *),
                                               0,
                                               NULL};

GOAT_NATIVE_API const goat_native_module_v1_t *GOAT_NATIVE_CALL
goat_native_query_v1(uint32_t version) {
    return version == GOAT_NATIVE_ABI_VERSION ? &module : NULL;
}
