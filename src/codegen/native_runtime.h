/** @file native_runtime.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Exports the bundled native support headers beside a C source file.
 */
#pragma once
#include <stdbool.h>

/** @brief Writes missing headers; accepts identical files and never overwrites different files. */
bool write_native_runtime_headers(const char *source_path);
/** @brief Removes headers from a private compilation workspace only. */
void remove_native_runtime_headers(const char *source_path);
