/** @file native_runtime.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Exports the bundled native support headers and archive beside a C source file.
 */
#pragma once
#include <stdbool.h>

/** @brief Writes missing SDK files; accepts identical files and never overwrites different files.
 */
bool write_native_runtime_files(const char *source_path);
/** @brief Removes SDK files from a private compilation workspace only. */
void remove_native_runtime_files(const char *source_path);

/** @brief Stable filename used for the exported and privately staged static library. */
#define GOAT_NATIVE_ARCHIVE_NAME "libgoat_native.a"

/** @brief Stage public headers and runtime C sources for compilation with the selected target.
 * Never uses the host archive. Same collision rules as write_native_runtime_files.
 */
bool write_native_runtime_sources(const char *source_path);
