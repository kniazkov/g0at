/**
 * @file arena.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definitions of structures and function prototypes for a memory arena.
 */

#pragma once

#include "alignment.h"
#include "value.h"

#include <stddef.h>
#include <wchar.h>

typedef struct arena_t arena_t;

typedef struct chunk_t chunk_t;

/** @brief The memory chunk structure. */
struct chunk_t {
    /**
     * @brief Pointer to the next chunk in the chain.
     *
     * If this chunk is the last in the chain, this pointer will be `NULL`.
     */
    _Alignas(memory_alignment_t) struct chunk_t *next;

    /** @brief Pointer to the first byte of allocated memory in this chunk. */
    char *begin;

    /** @brief Remaining unused memory size in the current chunk. */
    size_t unized_size;
};

/** @brief The memory arena that manages memory allocation. */
struct arena_t {
    /**
     * @brief Pointer to the first chunk in the memory arena.
     *
     * If the arena is empty, this pointer will be NULL.
     */
    chunk_t *first_chunk;

    /** @brief Pointer to the current position in the current chunk. */
    char *ptr;

    /**
     * @brief Default size of a regular chunk in this arena.
     *
     * This value is specified when the arena is created and is used whenever a new regular chunk
     * must be allocated.
     */
    size_t chunk_size;
};

/** @brief The threshold size for a "big object" in the memory arena. */
#define BIG_OBJECT_SIZE 256

/**
 * @brief Creates an arena; chunk_size_kb == 0 selects one kilobyte.
 * Allocation failure or size overflow terminates the process.
 */
arena_t *create_arena(size_t chunk_size_kb);

/**
 * @brief Allocates storage aligned to _Alignof(memory_alignment_t); zero size reserves one byte.
 * Storage remains valid until destroy_arena(). Large objects get dedicated chunks.
 */
void *alloc_from_arena(arena_t *arena, size_t size);

/**
 * @brief Allocates a zero-initialized memory block from the arena.
 * @return A pointer to the zero-initialized memory block.
 */
void *alloc_zeroed_from_arena(arena_t *arena, size_t size);

/** @brief Copies an object to the specified memory arena. */
void *copy_object_to_arena(arena_t *arena, const void *object, size_t size);

/**
 * @brief Copies a wide-character string to the specified memory arena and returns as string view.
 * `length`: The length of the string (excluding the null terminator).
 */
string_view_t copy_string_to_arena(arena_t *arena, const wchar_t *string, size_t length);

/** @brief Formats a string using the memory arena and returns it. */
string_view_t format_string_to_arena(arena_t *arena, const wchar_t *format, ...);

/** @brief Releases the arena and all its storage. The arena must be non-NULL. */
void destroy_arena(arena_t *arena);

typedef struct parser_memory_t parser_memory_t;

/** @brief Managing parser memory arenas. */
struct parser_memory_t {
    arena_t *positions; /**< Memory arena for source positions. */

    arena_t *tokens; /**< Memory arena for tokens. */

    arena_t *graph; /**< Memory arena for AST nodes. */

    arena_t *errors; /**< Memory arena for compilation errors. */
};
