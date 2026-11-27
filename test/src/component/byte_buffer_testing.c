/** @file byte_buffer_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Byte storage lifecycle, aliasing, overflow and allocation accounting.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "lib/bytes/byte_buffer.h"
#include "lib/memory/allocate.h"

#include <assert.h>
#include <string.h>

/** @brief Compare data and the allocation invariant. */
static void expect(const byte_buffer_t *b, const uint8_t *data, size_t size) {
    assert(b->length == size && b->capacity >= size);
    assert(byte_buffer_owned_bytes(b) == b->capacity);
    if (size)
        assert(!memcmp(b->data, data, size));
}

int main(void) {
    size_t baseline = get_allocated_memory_size();
    byte_buffer_t a = {0}, b = {0};
    assert(byte_buffer_resize(&a, 0, 255));
    assert(byte_buffer_append(&a, &a, 0, 0));
    assert(byte_buffer_copy_range(&a, 0, &a, 0, 0));
    byte_buffer_shrink(&a);
    byte_buffer_destroy(&a);
    byte_buffer_destroy(&a);
    assert(!a.data && !a.capacity && !a.length);
    assert(byte_buffer_resize(&a, 3, 0));
    a.data[0] = 0xAA;
    a.data[1] = 0xBB;
    a.data[2] = 0xCC;
    assert(byte_buffer_resize(&a, 1, 255));
    assert(byte_buffer_resize(&a, 3, 0));
    expect(&a, (uint8_t[]){0xAA, 0, 0}, 3);
    size_t capacity = a.capacity;
    uint8_t *data = a.data;
    byte_buffer_clear(&a);
    assert(a.capacity == capacity && a.data == data);
    assert(byte_buffer_resize(&a, 2, 255));
    expect(&a, (uint8_t[]){255, 255}, 2);
    assert(byte_buffer_copy(&b, &a));
    b.data[0] = 0;
    assert(a.data[0] == 255 && b.data != a.data);
    assert(byte_buffer_copy(&a, &a));
    assert(byte_buffer_append(&b, &a, 0, 2));
    expect(&b, (uint8_t[]){0, 255, 255, 255}, 4);
    byte_buffer_shrink(&a);
    assert(a.capacity == a.length);
    assert(byte_buffer_append(&a, &a, 0, 2)); // forced replacement
    expect(&a, (uint8_t[]){255, 255, 255, 255}, 4);
    a.data[0] = 0;
    a.data[1] = 1;
    a.data[2] = 2;
    a.data[3] = 3;
    assert(byte_buffer_reserve(&a, 16));
    data = a.data;
    assert(byte_buffer_append(&a, &a, 1, 2)); // spare capacity
    assert(a.data == data);
    expect(&a, (uint8_t[]){0, 1, 2, 3, 1, 2}, 6);
    byte_buffer_shrink(&a);
    assert(byte_buffer_append(&a, &a, 1, 3)); // subrange with replacement
    expect(&a, (uint8_t[]){0, 1, 2, 3, 1, 2, 1, 2, 3}, 9);
    assert(byte_buffer_resize(&a, 4, 0));
    assert(byte_buffer_copy_range(&a, 1, &a, 0, 3));
    expect(&a, (uint8_t[]){0, 0, 1, 2}, 4);
    assert(byte_buffer_copy_range(&a, 0, &a, 1, 3));
    expect(&a, (uint8_t[]){0, 1, 2, 2}, 4);
    assert(byte_buffer_copy_range(&b, 0, &a, 0, 4));
    expect(&b, a.data, 4);
    assert(byte_buffer_copy_range(&a, 4, &a, 4, 0));
    assert(!byte_buffer_copy_range(&a, 5, &a, 0, 0));
    assert(!byte_buffer_copy_range(&a, 0, &a, 5, 0));
    assert(!byte_buffer_copy_range(&a, 0, &a, 0, SIZE_MAX));
    assert(!byte_buffer_append(&a, &a, SIZE_MAX, 1));
    assert(!byte_buffer_append(&a, &a, 0, SIZE_MAX));
    data = a.data;
    capacity = a.capacity;
    assert(!byte_buffer_resize(&a, SIZE_MAX, 0));
    assert(a.data == data && a.capacity == capacity);
    expect(&a, (uint8_t[]){0, 1, 2, 2}, 4);
    size_t limit = byte_buffer_limit(), next = 123;
    assert(limit <= allocation_max_payload_size());
    assert((uintmax_t)limit <= INT64_MAX);
    assert(byte_buffer_capacity(limit - 1, limit, &next) && next == limit);
    assert(byte_buffer_capacity(0, limit, &next) && next == limit);
    assert(!byte_buffer_capacity(0, SIZE_MAX, &next) && next == limit);
    assert(!byte_buffer_capacity(SIZE_MAX, 0, &next) && next == limit);
    // Synthetic valid-size metadata exercises append addition before allocation/access.
    byte_buffer_t boundary = {NULL, limit, limit};
    assert(!byte_buffer_append(&boundary, &a, 0, 1));
    assert(boundary.length == limit && !boundary.data);
    byte_buffer_destroy(&a);
    byte_buffer_destroy(&b);
    // Repeated growth is geometric; shrink/reset does not resurrect old bytes.
    for (size_t cycle = 0; cycle < 100; ++cycle) {
        size_t changes = 0, previous = 0;
        for (size_t n = 1; n <= 1024; ++n) {
            assert(byte_buffer_resize(&a, n, (uint8_t)n));
            assert(a.data[n - 1] == (uint8_t)n);
            if (a.capacity != previous) {
                ++changes;
                previous = a.capacity;
            }
        }
        assert(changes < 16);
        for (size_t n = 0; n < 1024; ++n)
            assert(a.data[n] == (uint8_t)(n + 1));
        assert(get_allocated_memory_size() == baseline + a.capacity);
        byte_buffer_clear(&a);
        byte_buffer_shrink(&a);
        assert(!a.data && !a.capacity);
        assert(get_allocated_memory_size() == baseline);
    }
    byte_buffer_destroy(&a);
    assert(get_allocated_memory_size() == baseline);
    return 0;
}
