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

/** @brief Borrowed, immutable sequence of the host's wchar_t code units.
 * length counts code units, not bytes or Unicode characters, and excludes any terminator.
 * The first length units are readable; no trailing NUL is required. Embedded zero units
 * are allowed. data may be NULL only when length is zero. A view never owns storage and
 * must not be passed to deallocate. Its lifetime is bounded by the backing storage and
 * the outer native call. Substring views may point into the middle of another string.
 * Host and module must use the same wchar_t representation (including its width).
 */
typedef struct goat_native_string_view_v2_t {
    const wchar_t *data; ///< Borrowed code units; NULL is valid for an empty view.
    size_t length;       ///< Number of readable code units.
} goat_native_string_view_v2_t;

/** @brief Mutable string storage allocated through the current native context.
 * The zero-initialized value is an empty buffer with no allocation. Otherwise data is
 * the original pointer returned by goat_native_allocate for at least capacity + 1
 * wchar_t units, length <= capacity, and data[length] is zero. capacity excludes the
 * terminator; embedded zeros are allowed and length remains authoritative. Check
 * capacity + 1 and its byte-size multiplication for overflow before allocation.
 * Only the owning buffer may mutate or individually deallocate the block, using the
 * same context. Struct copies do not create new owners. Views borrow its storage and
 * become invalid when it is released or replaced. After individual release reset all
 * fields to zero. Outstanding storage is reclaimed by the outer call's bulk cleanup;
 * the interpreter must consume/copy any result before that cleanup. No VM object or
 * reference-counted ownership crosses this boundary.
 */
typedef struct goat_native_string_buffer_v2_t {
    wchar_t *data;   ///< Context allocation, or NULL when length and capacity are zero.
    size_t length;   ///< Number of used code units, excluding the trailing zero.
    size_t capacity; ///< Number of available code units, excluding the trailing zero.
} goat_native_string_buffer_v2_t;

typedef struct goat_native_value_v2_t {
    uint32_t type;
    uint32_t reserved;

    union {
        int64_t integer;
        double real;
    } value;
} goat_native_value_v2_t;

/** @brief Interpreter service table. Append future services after this memory prefix.
 * struct_size is the size supplied by the host; consumers check the prefix they use.
 * Callback code executes in the host, never in the generated module.
 * allocate returns fundamentally aligned, uninitialized storage; zero size reserves
 * a distinct live block. OOM and size overflow are fatal, never a NULL return.
 * deallocate(NULL) is harmless. Other pointers must be live blocks from this context.
 * Only the outer context owner bulk-clears memory, after consuming results, on success
 * and on failure. Callees may individually release their own temporaries.
 */
typedef struct goat_native_services_v2_t {
    uint32_t struct_size;
    void *(GOAT_NATIVE_CALL *allocate)(void *state, size_t size);
    void(GOAT_NATIVE_CALL *deallocate)(void *state, void *pointer);
    void(GOAT_NATIVE_CALL *deallocate_all)(void *state);
} goat_native_services_v2_t;

/** @brief Borrowed host context. State is opaque to native code and thread-confined.
 * Independent outer calls have independent states, including reentrant calls.
 * Nested generated calls share the same context. Neither state nor allocated pointers
 * may outlive the outer call. The immutable service table may be shared by all threads.
 */
typedef struct goat_native_context_v2_t {
    const goat_native_services_v2_t *services;
    void *state;
} goat_native_context_v2_t;

/** @brief Size of the required memory-service prefix, independent of future additions. */
#    define GOAT_NATIVE_MEMORY_SERVICES_SIZE                                                       \
        (offsetof(goat_native_services_v2_t, deallocate_all)                                       \
         + sizeof(((goat_native_services_v2_t *)0)->deallocate_all))

/** @brief Checks the memory-service prefix before entering a generated module. */
static inline int goat_native_has_memory_services(const goat_native_context_v2_t *context) {
    return context && context->services
           && context->services->struct_size >= GOAT_NATIVE_MEMORY_SERVICES_SIZE
           && context->services->allocate && context->services->deallocate
           && context->services->deallocate_all;
}

/** @brief Dispatches allocation to the interpreter; contains no allocation implementation. */
static inline void *goat_native_allocate(const goat_native_context_v2_t *context, size_t size) {
    return context->services->allocate(context->state, size);
}

/** @brief Dispatches individual release to the interpreter. */
static inline void goat_native_deallocate(const goat_native_context_v2_t *context, void *pointer) {
    context->services->deallocate(context->state, pointer);
}

/** @brief Dispatches bulk cleanup; only the outer context owner may invoke it. */
static inline void goat_native_deallocate_all(const goat_native_context_v2_t *context) {
    context->services->deallocate_all(context->state);
}

typedef uint32_t(GOAT_NATIVE_CALL *goat_native_adapter_v2_t)(
    uint32_t version,
    const goat_native_context_v2_t *context,
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
