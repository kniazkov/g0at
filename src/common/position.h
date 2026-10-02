/**
 * @file position.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines structures for representing positions and ranges of entities in source code.
 */

#pragma once

#include <stddef.h>

typedef struct arena_t arena_t;

/** @brief The full position of an entity in the source code. */
typedef struct {
    /** @brief The name of the source file. */
    const char *file_name;

    /** @brief The starting row (line) number of the entity. */
    size_t row;

    /** @brief The starting column number of the entity. */
    size_t column;

    /** @brief Pointer to the source text at the entity position. */
    const wchar_t *code;

    /** @brief Offset of the entity from the beginning of the file. */
    size_t offset;
} full_position_t;

/** @brief The shortened position of an entity in the source code. */
typedef struct {
    /** @brief The row (line) number of the entity. */
    size_t row;

    /** @brief The column number of the entity. */
    size_t column;

    /** @brief Offset of the entity from the beginning of the file. */
    size_t offset;
} short_position_t;

/** @brief A source range occupied by an entity. */
typedef struct {
    /** @brief Full position of the beginning of the range. */
    full_position_t *begin;

    /** @brief Shortened position of the end of the range. */
    short_position_t *end;
} position_range_t;

/**
 * @brief Copies a full position into the specified memory arena.
 * @return Pointer to the copied full position in arena memory, or `NULL` if `position` is `NULL`.
 */
full_position_t *copy_full_position_to_arena(arena_t *arena, const full_position_t *position);

/**
 * @brief Creates a shortened copy of a full position in the specified memory arena.
 * @return Pointer to the created short position in arena memory, or `NULL` if `position` is `NULL`.
 */
short_position_t *create_short_position_from_full(arena_t *arena, const full_position_t *position);

/** @brief Creates a new source range in the specified memory arena. */
position_range_t *
create_position_range(arena_t *arena, full_position_t *begin, short_position_t *end);
