/** @file native_abi.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Version 2 native boundary, independent of VM objects and analysis storage.
 */
/* ABI declarations begin. */
#ifndef GOAT_NATIVE_ABI_V2_H
#    define GOAT_NATIVE_ABI_V2_H
#    include <stddef.h>
#    include <stdint.h>

#    ifdef _WIN32
#        define GOAT_NATIVE_CALL __cdecl
#        ifdef GOAT_NATIVE_BUILD
#            define GOAT_NATIVE_API __declspec(dllexport)
#        else
#            define GOAT_NATIVE_API
#        endif
#    else
#        define GOAT_NATIVE_CALL
#        define GOAT_NATIVE_API
#    endif

enum {
    GOAT_NATIVE_ABI_VERSION = 2,
    GOAT_NATIVE_INVALID = 0,
    GOAT_NATIVE_I64 = 1,
    GOAT_NATIVE_F64 = 2
};

enum {
    GOAT_NATIVE_OK = 0,
    GOAT_NATIVE_TYPE_MISMATCH = 1,
    GOAT_NATIVE_BAD_REQUEST = 2,
    GOAT_NATIVE_ABI_MISMATCH = 3,
    GOAT_NATIVE_RESOURCE_LIMIT = 4,
    GOAT_NATIVE_EXTERNAL_ERROR = 5
};

enum { GOAT_NATIVE_PURE = 1 };

#    ifdef __cplusplus
extern "C" {
#    endif

typedef struct goat_native_value_v2_t {
    uint32_t type;
    uint32_t reserved;

    union {
        int64_t integer;
        double real;
    } value;
} goat_native_value_v2_t;

/** @brief Host-owned allocation service, valid until the outer invocation completes.
 * allocate never returns NULL: allocation failure and size overflow terminate the process.
 * A zero-byte allocation returns a distinct live block. Storage supports fundamental
 * alignment (not explicitly over-aligned types). deallocate(NULL) is a no-op.
 * Only live blocks from this instance may be freed; double-free is a contract violation.
 * deallocate_all invalidates all remaining blocks and leaves the allocator reusable.
 * Independent instances may be used concurrently; one instance is thread-confined.
 * Callees borrow the service and pass it to nested calls. Only its owner may bulk-clear
 * it, after consuming results and after every successful or failed invocation.
 */
typedef struct goat_native_allocator_v2_t {
    uint32_t struct_size;
    void *context;
    void *(GOAT_NATIVE_CALL *allocate)(void *context, size_t size);
    void(GOAT_NATIVE_CALL *deallocate)(void *context, void *pointer);
    void(GOAT_NATIVE_CALL *deallocate_all)(void *context);
} goat_native_allocator_v2_t;

typedef uint32_t(GOAT_NATIVE_CALL *goat_native_adapter_v2_t)(
    uint32_t version,
    const goat_native_allocator_v2_t *allocator,
    uint32_t argument_count,
    const goat_native_value_v2_t *arguments,
    goat_native_value_v2_t *result);

typedef struct goat_native_entry_v2_t {
    uint64_t specialization_id;
    uint64_t function_id;
    uint32_t parameter_count;
    uint32_t return_type;
    const uint32_t *parameter_types;
    goat_native_adapter_v2_t invoke;
    const char *binding_name;
    uint32_t flags;
} goat_native_entry_v2_t;

typedef struct goat_native_module_v2_t {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t value_size;
    uint32_t value_alignment;
    uint32_t entry_size;
    uint32_t pointer_size;
    uint32_t entry_count;
    const goat_native_entry_v2_t *entries;
} goat_native_module_v2_t;

typedef const goat_native_module_v2_t *(GOAT_NATIVE_CALL *goat_native_query_v2_t)(uint32_t version);
GOAT_NATIVE_API const goat_native_module_v2_t *GOAT_NATIVE_CALL
goat_native_query_v2(uint32_t version);
#    ifdef __cplusplus
}
#    endif
#endif
/* ABI declarations end. */
