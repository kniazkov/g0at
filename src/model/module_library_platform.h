/** @file module_library_platform.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Windows-only raw image helpers, without VM types or opcodes.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef _WIN32
bool module_windows_image_range(void *handle, const void *pointer, size_t length, bool code);
void *module_windows_symbol_address(void *handle, const char *symbol);
#endif
