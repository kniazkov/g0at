/** @file byte_buffer.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Exclusively owned compact bytes; no VM objects or implicit GC.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Initialize with {0}; never shallow-copy an owning buffer.
 * All functions require valid initialized buffers and exclusive access.
 * Destroy resets the structure and may be repeated. False means invalid size or
 * range with no mutation; allocation failure follows the fatal ALLOC policy.
 */
typedef struct {
    uint8_t *data;   /**< Owned allocation, NULL at zero capacity. */
    size_t length;   /**< Initialized bytes. */
    size_t capacity; /**< Allocated bytes, not including allocator metadata. */
} byte_buffer_t;

/** @brief Runtime limit, also respecting allocator metadata and Integer range. */
size_t byte_buffer_limit(void);
/** @brief Compute geometric capacity without allocation; false leaves output unchanged. */
bool byte_buffer_capacity(size_t current, size_t needed, size_t *result);
/** @brief Ensure capacity without changing length; existing views may be invalidated. */
bool byte_buffer_reserve(byte_buffer_t *buffer, size_t needed);
/** @brief Change length, filling only newly exposed bytes. */
bool byte_buffer_resize(byte_buffer_t *buffer, size_t length, uint8_t fill);
/** @brief Set length to zero while retaining capacity. */
void byte_buffer_clear(byte_buffer_t *buffer);
/** @brief Reduce capacity to length, freeing empty storage. */
void byte_buffer_shrink(byte_buffer_t *buffer);
/** @brief Free storage and reset to zero; safe on empty or already destroyed storage. */
void byte_buffer_destroy(byte_buffer_t *buffer);
/** @brief Independently copy bytes into initialized destination; self-copy is a no-op. */
bool byte_buffer_copy(byte_buffer_t *target, const byte_buffer_t *source);
/** @brief Append an existing range; source==target is supported across growth.
 * Distinct buffers must not share owning allocations. No raw borrowed pointer is
 * accepted, so an offset can be reacquired safely after storage replacement.
 */
bool byte_buffer_append(byte_buffer_t *target,
                        const byte_buffer_t *source,
                        size_t start,
                        size_t count);
/** @brief Copy existing ranges without resizing; overlap has memmove semantics. */
bool byte_buffer_copy_range(byte_buffer_t *target,
                            size_t target_start,
                            const byte_buffer_t *source,
                            size_t source_start,
                            size_t count);
/** @brief Dynamic bytes for owner GC accounting; excludes the buffer structure. */
size_t byte_buffer_owned_bytes(const byte_buffer_t *buffer);
