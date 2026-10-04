/** @file native_abi.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Version 1 numeric boundary, independent of VM objects and analysis storage.
 */
/* ABI declarations begin. */
#ifndef GOAT_NATIVE_ABI_V1_H
#    define GOAT_NATIVE_ABI_V1_H
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
    GOAT_NATIVE_ABI_VERSION = 1,
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

typedef struct goat_native_value_v1_t {
    uint32_t type;
    uint32_t reserved;

    union {
        int64_t integer;
        double real;
    } value;
} goat_native_value_v1_t;

typedef uint32_t(GOAT_NATIVE_CALL *goat_native_adapter_v1_t)(
    uint32_t version,
    uint32_t argument_count,
    const goat_native_value_v1_t *arguments,
    goat_native_value_v1_t *result);

typedef struct goat_native_entry_v1_t {
    uint64_t specialization_id;
    uint64_t function_id;
    uint32_t parameter_count;
    uint32_t return_type;
    const uint32_t *parameter_types;
    goat_native_adapter_v1_t invoke;
    const char *binding_name;
    uint32_t flags;
} goat_native_entry_v1_t;

typedef struct goat_native_module_v1_t {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t value_size;
    uint32_t value_alignment;
    uint32_t entry_size;
    uint32_t pointer_size;
    uint32_t entry_count;
    const goat_native_entry_v1_t *entries;
} goat_native_module_v1_t;

typedef const goat_native_module_v1_t *(GOAT_NATIVE_CALL *goat_native_query_v1_t)(uint32_t version);
GOAT_NATIVE_API const goat_native_module_v1_t *GOAT_NATIVE_CALL
goat_native_query_v1(uint32_t version);
#    ifdef __cplusplus
}
#    endif
#endif
/* ABI declarations end. */
