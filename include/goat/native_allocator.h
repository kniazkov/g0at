/** @file native_allocator.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Lazy host implementation of the native ABI allocation service.
 */
#ifndef GOAT_NATIVE_ALLOCATOR_H
#define GOAT_NATIVE_ALLOCATOR_H
#include "native_abi.h"

#include <stdio.h>
#include <stdlib.h>

/** @brief Fundamental alignment, including MinGW i386 extended scalar types. */
typedef union {
    max_align_t standard;
#if defined(__i386__) && defined(__SIZEOF_FLOAT128__)
    __float128 extended;
#endif
} goat_native_alignment_t;

typedef struct goat_native_block_t goat_native_block_t;

/** @brief Intrusive allocation list; the payload immediately follows this header. */
struct goat_native_block_t {
    _Alignas(goat_native_alignment_t) void *allocation;
    goat_native_block_t *previous;
    goat_native_block_t *next;
};

/** @brief Zero-initialize before use; never copy a state that owns live blocks. */
typedef struct goat_native_allocator_state_t {
    goat_native_block_t *first;
} goat_native_allocator_state_t;

static inline void *GOAT_NATIVE_CALL goat_native_allocate(void *context, size_t size) {
    goat_native_allocator_state_t *state = context;
    if (!size)
        size = 1;
    const size_t alignment = _Alignof(goat_native_alignment_t);
    if (size > SIZE_MAX - sizeof(goat_native_block_t) - (alignment - 1)) {
        fputs("\nAllocation size overflow.\n", stderr);
        exit(EXIT_FAILURE);
    }
    void *allocation = malloc(sizeof(goat_native_block_t) + size + alignment - 1);
    if (!allocation) {
        fputs("\nOut of memory.\n", stderr);
        exit(EXIT_FAILURE);
    }
    size_t offset = (alignment - (uintptr_t)allocation % alignment) % alignment;
    goat_native_block_t *block = (goat_native_block_t *)((char *)allocation + offset);
    block->allocation = allocation;
    block->previous = NULL;
    block->next = state->first;
    if (state->first)
        state->first->previous = block;
    state->first = block;
    return block + 1;
}

static inline void GOAT_NATIVE_CALL goat_native_deallocate(void *context, void *pointer) {
    if (!pointer)
        return;
    goat_native_allocator_state_t *state = context;
    goat_native_block_t *block = (goat_native_block_t *)pointer - 1;
    if (block->previous)
        block->previous->next = block->next;
    else
        state->first = block->next;
    if (block->next)
        block->next->previous = block->previous;
    free(block->allocation);
}

static inline void GOAT_NATIVE_CALL goat_native_deallocate_all(void *context) {
    goat_native_allocator_state_t *state = context;
    while (state->first)
        goat_native_deallocate(context, state->first + 1);
}

/** @brief Creates a service descriptor without allocating or touching the heap. */
static inline goat_native_allocator_v2_t
goat_native_allocator_init(goat_native_allocator_state_t *state) {
    goat_native_allocator_v2_t allocator = {sizeof(goat_native_allocator_v2_t),
                                            state,
                                            goat_native_allocate,
                                            goat_native_deallocate,
                                            goat_native_deallocate_all};
    return allocator;
}
#endif
