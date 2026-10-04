/** @file native_compiler.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Synchronous native compilation, independent of the VM and loader.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef enum {
    NATIVE_COMPILE_OK,
    NATIVE_COMPILE_UNSUPPORTED,
    NATIVE_COMPILE_IO_ERROR,
    NATIVE_COMPILE_START_ERROR,
    NATIVE_COMPILE_FAILED
} native_compile_status_t;

typedef struct {
    native_compile_status_t status;
    int system_error;
    int exit_code;
    int signal_number;
    int cleanup_error;
    char *diagnostics;
    size_t diagnostics_length;
    bool diagnostics_truncated;
} native_compile_result_t;

/** @brief Builds beside destination, then atomically replaces it; compiler is one executable. */
native_compile_result_t
compile_native_library(const wchar_t *source, const char *compiler, const char *destination);
/** @brief Releases captured diagnostics. */
void destroy_native_compile_result(native_compile_result_t *result);
