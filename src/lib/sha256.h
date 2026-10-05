/** @file sha256.h
 * @copyright 2026 Ivan Kniazkov
 * @brief SHA-256 digests of byte buffers, without external dependencies.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define SHA256_SIZE 32

/** @brief Hashes size bytes; data may be NULL only when size is zero. */
void sha256(const void *data, size_t size, uint8_t digest[SHA256_SIZE]);
