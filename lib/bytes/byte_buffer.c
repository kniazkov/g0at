/** @file byte_buffer.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Checked byte storage with geometric growth and alias-safe range operations.
 */
#include "byte_buffer.h"

#include "lib/memory/allocate.h"

#include <string.h>

size_t byte_buffer_limit(void) {
    size_t limit = allocation_max_payload_size();
    return (uintmax_t)limit > INT64_MAX ? (size_t)INT64_MAX : limit;
}

bool byte_buffer_capacity(size_t current, size_t needed, size_t *result) {
    size_t limit = byte_buffer_limit();
    if (current > limit || needed > limit)
        return false;
    size_t next = current;
    if (next < needed) {
        if (!next)
            next = 16;
        while (next < needed)
            next = next > limit / 2 ? limit : next * 2;
    }
    *result = next;
    return true;
}

/** @brief Replace storage while preserving initialized bytes; capacity >= length. */
static void replace_storage(byte_buffer_t *buffer, size_t capacity) {
    uint8_t *data = capacity ? ALLOC(capacity) : NULL;
    if (buffer->length)
        memcpy(data, buffer->data, buffer->length);
    FREE(buffer->data);
    buffer->data = data;
    buffer->capacity = capacity;
}

bool byte_buffer_reserve(byte_buffer_t *buffer, size_t needed) {
    size_t capacity;
    if (!byte_buffer_capacity(buffer->capacity, needed, &capacity))
        return false;
    if (capacity != buffer->capacity)
        replace_storage(buffer, capacity);
    return true;
}

bool byte_buffer_resize(byte_buffer_t *buffer, size_t length, uint8_t fill) {
    if (!byte_buffer_reserve(buffer, length))
        return false;
    if (length > buffer->length)
        memset(buffer->data + buffer->length, fill, length - buffer->length);
    buffer->length = length;
    return true;
}

void byte_buffer_clear(byte_buffer_t *buffer) {
    buffer->length = 0;
}

void byte_buffer_destroy(byte_buffer_t *buffer) {
    FREE(buffer->data);
    *buffer = (byte_buffer_t){0};
}

void byte_buffer_shrink(byte_buffer_t *buffer) {
    if (buffer->capacity != buffer->length)
        replace_storage(buffer, buffer->length);
}

bool byte_buffer_copy(byte_buffer_t *target, const byte_buffer_t *source) {
    if (target == source)
        return true;
    if (!byte_buffer_reserve(target, source->length))
        return false;
    if (source->length)
        memcpy(target->data, source->data, source->length);
    target->length = source->length;
    return true;
}

/** @brief Subtraction avoids overflow and accepts an empty range at the end. */
static bool valid_range(size_t length, size_t start, size_t count) {
    return start <= length && count <= length - start;
}

bool byte_buffer_append(byte_buffer_t *target,
                        const byte_buffer_t *source,
                        size_t start,
                        size_t count) {
    if (!valid_range(source->length, start, count) || count > byte_buffer_limit() - target->length)
        return false;
    size_t old_length = target->length;
    if (!byte_buffer_reserve(target, old_length + count))
        return false;
    if (count)
        memmove(target->data + old_length, source->data + start, count);
    target->length = old_length + count;
    return true;
}

bool byte_buffer_copy_range(byte_buffer_t *target,
                            size_t target_start,
                            const byte_buffer_t *source,
                            size_t source_start,
                            size_t count) {
    if (!valid_range(target->length, target_start, count)
        || !valid_range(source->length, source_start, count))
        return false;
    if (count)
        memmove(target->data + target_start, source->data + source_start, count);
    return true;
}

size_t byte_buffer_owned_bytes(const byte_buffer_t *buffer) {
    return allocation_payload_size(buffer->data);
}
