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

/** Incremental state; initialize before updates, finish once after the last update.
 * Total input must be at most UINT64_MAX / 8 bytes (SHA-256 bit length).
 */
typedef struct sha256_context {
    uint32_t state[8];
    uint64_t size;
    uint8_t tail[64];
    size_t used;
} sha256_context;

void sha256_init(sha256_context *ctx);
void sha256_update(sha256_context *ctx, const void *data, size_t size);
void sha256_finish(sha256_context *ctx, uint8_t digest[SHA256_SIZE]);
