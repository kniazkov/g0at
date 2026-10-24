/** @file module_abi.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Fixed module-v1 scalar ABI, separate from the legacy native query ABI.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#    define GOAT_MODULE_CALL __cdecl
#    define GOAT_MODULE_EXPORT __declspec(dllexport)
#else
#    define GOAT_MODULE_CALL
#    define GOAT_MODULE_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
#    define GOAT_MODULE_ALIGN alignas(8)
#    define GOAT_MODULE_ASSERT static_assert
extern "C" {
#else
#    define GOAT_MODULE_ALIGN _Alignas(8)
#    define GOAT_MODULE_ASSERT _Static_assert
#endif

enum {
    GOAT_MODULE_ABI_VERSION = 1,
    GOAT_MODULE_NULL = 0,
    GOAT_MODULE_INTEGER = 1,
    GOAT_MODULE_REAL = 2,
    GOAT_MODULE_STRING = 3,
    GOAT_MODULE_BOOLEAN = 4,
    GOAT_MODULE_CHARACTER = 5
};

enum {
    GOAT_MODULE_SUCCESS = 0,
    GOAT_MODULE_TYPE_MISMATCH = 1,
    GOAT_MODULE_BAD_REQUEST = 2,
    GOAT_MODULE_ABI_MISMATCH = 3,
    GOAT_MODULE_RESOURCE_LIMIT = 4,
    GOAT_MODULE_EXTERNAL_ERROR = 5
};

/** Borrowed Unicode scalars, no terminator required. All padding must be zero.
 * Storage lives through the outer call. Copy results before releasing context.
 */
typedef struct goat_module_string_v1 {
    GOAT_MODULE_ALIGN const uint32_t *data;
#if UINTPTR_MAX == UINT32_MAX
    uint32_t reserved;
#endif
    uint64_t length;
} goat_module_string_v1;

/** Fixed 16-byte scalar value. Pointer payloads are zero extended on x86/32. */
typedef struct goat_module_value_v1 {
    GOAT_MODULE_ALIGN uint32_t tag;
    uint32_t reserved;
    uint64_t payload;
} goat_module_value_v1;

/** Host-owned services. Native code must not substitute a private allocator.
 * allocate is fatal on overflow/OOM, never returns NULL; zero bytes creates a live
 * block. Only the outer owner bulk-releases after consuming the result. State is
 * opaque and call-confined. Character services use pinned Unicode 17.0.0.
 */
typedef struct goat_module_services_v1 {
    uint32_t struct_size;
    uint32_t reserved;
    void *(GOAT_MODULE_CALL *allocate)(void *state, uint64_t size);
    void(GOAT_MODULE_CALL *deallocate)(void *state, void *pointer);
    void(GOAT_MODULE_CALL *deallocate_all)(void *state);
    uint32_t(GOAT_MODULE_CALL *is_space)(uint32_t value);
    uint32_t(GOAT_MODULE_CALL *to_upper)(uint32_t value);
    uint32_t(GOAT_MODULE_CALL *to_lower)(uint32_t value);
} goat_module_services_v1;

typedef struct goat_module_context_v1 {
    const goat_module_services_v1 *services;
    void *state;
} goat_module_context_v1;

#define GOAT_MODULE_SERVICES_SIZE                                                                  \
    (offsetof(goat_module_services_v1, to_lower) + sizeof(((goat_module_services_v1 *)0)->to_lower))

typedef uint32_t(GOAT_MODULE_CALL *goat_module_adapter_v1)(uint32_t abi_version,
                                                           const goat_module_context_v1 *context,
                                                           uint32_t argument_count,
                                                           const goat_module_value_v1 *arguments,
                                                           goat_module_value_v1 *result);

GOAT_MODULE_ASSERT(sizeof(goat_module_value_v1) == 16, "module value size");
GOAT_MODULE_ASSERT(offsetof(goat_module_value_v1, payload) == 8, "module payload offset");
GOAT_MODULE_ASSERT(sizeof(goat_module_string_v1) == 16, "module string size");
GOAT_MODULE_ASSERT(offsetof(goat_module_string_v1, length) == 8, "module string length offset");
GOAT_MODULE_ASSERT(offsetof(goat_module_services_v1, allocate) == 8, "module services prefix");
#ifdef __cplusplus
}
#endif
