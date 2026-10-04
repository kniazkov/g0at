/** @file native_compiler_internal.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared diagnostics and platform compiler entry points.
 */
#pragma once
#include "native_compiler.h"
void native_compiler_read_diagnostics(native_compile_result_t *result, const char *path);
#ifdef _WIN32
native_compile_result_t compile_native_library_windows(const wchar_t *source,
                                                       const char *compiler,
                                                       const char *destination);
#endif
