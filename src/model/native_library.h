/** @file native_library.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Validated native metadata and shared library/function ownership.
 */
#pragma once
#include <goat/native_abi.h>
#include <stdbool.h>

typedef struct native_library_t native_library_t;
typedef struct native_function_descriptor_t native_function_descriptor_t;

typedef enum {
    NATIVE_LIBRARY_OK,
    NATIVE_LIBRARY_UNSUPPORTED,
    NATIVE_LIBRARY_OPEN_FAILED,
    NATIVE_LIBRARY_QUERY_MISSING,
    NATIVE_LIBRARY_ABI_MISMATCH,
    NATIVE_LIBRARY_INVALID_METADATA
} native_library_status_t;

typedef struct {
    native_library_status_t status;
    native_library_t *library;
    char *diagnostic;
} native_library_result_t;

/** @brief Loads trusted native code from an explicit path and snapshots validated metadata. */
native_library_result_t load_native_library(const char *path);
/** @brief Releases the result's library reference and diagnostic. */
void destroy_native_library_result(native_library_result_t *result);
native_library_t *retain_native_library(native_library_t *library);
void release_native_library(native_library_t *library);
uint32_t get_native_library_entry_count(const native_library_t *library);
/** @brief Borrowed metadata, valid while a library reference is held. */
const goat_native_entry_v1_t *get_native_library_entry(const native_library_t *library,
                                                       uint32_t index);

/** @brief Acquires all specializations of a module-local function; NULL means absent. */
native_function_descriptor_t *create_native_function_descriptor(native_library_t *library,
                                                                uint64_t function_id);
native_function_descriptor_t *
retain_native_function_descriptor(native_function_descriptor_t *function);
void release_native_function_descriptor(native_function_descriptor_t *function);
uint32_t get_native_function_entry_count(const native_function_descriptor_t *function);
/** @brief Borrowed entry; the descriptor keeps its library and code alive. */
const goat_native_entry_v1_t *
get_native_function_entry(const native_function_descriptor_t *function, uint32_t index);

/** @brief Platform helpers; diagnostics use the project allocator. */
void *open_native_library_handle(const char *path, char **diagnostic);
bool get_native_library_query(void *handle, goat_native_query_v1_t *query, char **diagnostic);
void close_native_library_handle(void *handle);
char *copy_native_library_diagnostic(const char *text);

/** @brief Requires conservative stack headroom before entering a native adapter. */
bool native_stack_has_headroom(void);

/** @brief Registers one owned resource, released after the final OS handle is closed. */
void set_native_library_cleanup(native_library_t *library, void *resource, void (*cleanup)(void *));
