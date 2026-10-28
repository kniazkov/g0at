/** @file format_binary.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent binary format oracle. Never calls Goat's binary/hash wrappers.
 */
#pragma once
#include "format_metadata.h"

/** malloc-owned, exact reference.py constructors. */
unsigned char *format_binary_minimal(size_t *size);
unsigned char *format_binary_importing(size_t *size);
bool format_binary_check(const unsigned char *data, size_t size, char *error, size_t error_size);
/** Validate a normalized copy; inputs are never modified. */
bool format_binary_check_module(yyjson_mut_val *metadata,
                                yyjson_mut_val *source,
                                const unsigned char *data,
                                size_t size,
                                char *error,
                                size_t error_size);
void format_binary_sha256(const void *data, size_t size, char hex[65]);
bool format_binary_main_hash(yyjson_mut_val *metadata,
                             yyjson_mut_val *source,
                             const unsigned char *data,
                             size_t size,
                             char hex[65],
                             char *error,
                             size_t error_size);
