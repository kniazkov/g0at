/** @file native_services.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Native host services: tracked ALLOC/FREE and interpreter-local character mappings.
 */
#include "native_services.h"

#include "goat/unicode.h"
#include "lib/alignment.h"
#include "lib/allocate.h"

#include <stdio.h>
#include <stdlib.h>

/** @brief Aligned list header included in the interpreter's tracked allocation. */
struct native_memory_block_t {
    _Alignas(memory_alignment_t) native_memory_block_t *previous;
    native_memory_block_t *next;
};

static void *GOAT_NATIVE_CALL allocate_memory(void *state, size_t size) {
    native_memory_t *memory = state;
    if (!size)
        size = 1;
    if (size > SIZE_MAX - sizeof(native_memory_block_t)) {
        fputs("\nAllocation size overflow.\n", stderr);
        exit(EXIT_FAILURE);
    }
    native_memory_block_t *block = ALLOC(sizeof(*block) + size);
    block->previous = NULL;
    block->next = memory->first;
    if (memory->first)
        memory->first->previous = block;
    memory->first = block;
    return block + 1;
}

static void GOAT_NATIVE_CALL deallocate_memory(void *state, void *pointer) {
    if (!pointer)
        return;
    native_memory_t *memory = state;
    native_memory_block_t *block = (native_memory_block_t *)pointer - 1;
    if (block->previous)
        block->previous->next = block->next;
    else
        memory->first = block->next;
    if (block->next)
        block->next->previous = block->previous;
    FREE(block);
}

static void GOAT_NATIVE_CALL deallocate_all_memory(void *state) {
    native_memory_t *memory = state;
    while (memory->first)
        deallocate_memory(state, memory->first + 1);
}

/** @brief Uses the same pinned Unicode classification as the interpreter. */
static int GOAT_NATIVE_CALL string_is_space(gchar_t value) {
    return ISSPACE(value);
}

/** @brief Uses the pinned Unicode 17.0.0 simple mapping. */
static gchar_t GOAT_NATIVE_CALL string_to_upper(gchar_t value) {
    return (gchar_t)TOUPPER(value);
}

/** @brief Uses the pinned Unicode 17.0.0 simple mapping. */
static gchar_t GOAT_NATIVE_CALL string_to_lower(gchar_t value) {
    return (gchar_t)TOLOWER(value);
}

/** @brief Shared immutable table; mutable state belongs exclusively to each invocation. */
static const goat_native_services_v2_t services = {sizeof(goat_native_services_v2_t),
                                                   allocate_memory,
                                                   deallocate_memory,
                                                   deallocate_all_memory,
                                                   string_is_space,
                                                   string_to_upper,
                                                   string_to_lower};

goat_native_context_v2_t init_native_context(native_memory_t *memory) {
    return (goat_native_context_v2_t){&services, memory};
}
