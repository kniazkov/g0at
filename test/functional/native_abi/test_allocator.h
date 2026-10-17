/** @file test_allocator.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Invocation ownership for native ABI test hosts.
 */
#pragma once
#include <goat/native_allocator.h>

static inline uint32_t test_invoke(goat_native_adapter_v2_t invoke,
                                   uint32_t version,
                                   uint32_t count,
                                   const goat_native_value_v2_t *args,
                                   goat_native_value_v2_t *result) {
    goat_native_allocator_state_t state = {0};
    goat_native_allocator_v2_t allocator = goat_native_allocator_init(&state);
    uint32_t status = invoke(version, &allocator, count, args, result);
    allocator.deallocate_all(allocator.context);
    return status;
}
