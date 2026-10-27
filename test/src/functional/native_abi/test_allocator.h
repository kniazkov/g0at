/** @file test_allocator.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Invocation ownership for native ABI test hosts.
 */
#pragma once
#include <goat/native_abi.h>
#include <stdlib.h>

static inline void *GOAT_NATIVE_CALL test_unexpected_allocate(void *state, size_t size) {
    (void)state;
    (void)size;
    abort();
}

static inline void GOAT_NATIVE_CALL test_unexpected_deallocate(void *state, void *pointer) {
    (void)state;
    if (pointer)
        abort();
}

static inline void GOAT_NATIVE_CALL test_clear(void *state) {
    (void)state;
}

/** @brief Numeric fixtures prove that passing a context does not require heap allocation. */
static inline goat_native_context_v2_t test_context(void) {
    static const goat_native_services_v2_t services = {
        .struct_size = GOAT_NATIVE_STRING_SERVICES_SIZE,
        .allocate = test_unexpected_allocate,
        .deallocate = test_unexpected_deallocate,
        .deallocate_all = test_clear,
    };
    return (goat_native_context_v2_t){&services, NULL};
}

static inline uint32_t test_invoke(goat_native_adapter_v2_t invoke,
                                   uint32_t version,
                                   uint32_t count,
                                   const goat_native_value_v2_t *args,
                                   goat_native_value_v2_t *result) {
    goat_native_context_v2_t allocator = test_context();
    uint32_t status = invoke(version, &allocator, count, args, result);
    goat_native_deallocate_all(&allocator);
    return status;
}
