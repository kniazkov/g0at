/** @file native_library_platform.h
 * @copyright 2026 Ivan Kniazkov
 * @brief OS image ownership helpers shared by the legacy and module-v1 loaders.
 */
#pragma once
#include <stdbool.h>

/** @brief Platform helpers; diagnostics use the project allocator. */
void *open_native_library_handle(const char *path, char **diagnostic);
void close_native_library_handle(void *handle);
char *copy_native_library_diagnostic(const char *text);

/** @brief Requires conservative stack headroom before entering a native adapter. */
bool native_stack_has_headroom(void);
