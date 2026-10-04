/** @file binary_file.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Bounded binary reads and atomic replacement.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Reads at most limit bytes; release the returned buffer with FREE. */
void *read_binary_file(const char *path, size_t limit, size_t *size);
bool write_binary_file(const char *path, const void *data, size_t size);
/** @brief FNV-1a integrity checksum, not an authenticity guarantee. */
uint64_t binary_checksum(const void *data, size_t size);
