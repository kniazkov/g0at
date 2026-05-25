/**
 * @file position.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines structures for representing positions and ranges of entities in source code.
 *
 * This file contains definitions of source position structures used to store
 * information about where an entity is located in the source code. A position
 * is uniquely defined by the file name, line number, column number, and offset.
 * Both full and shortened position forms are provided, as well as a range
 * structure that describes the span of an entity from its beginning to its end.
 */

#pragma once

#include <stddef.h>

/**
 * @typedef arena_t
 * @brief Forward declaration for the arena memory allocator.
 */
typedef struct arena_t arena_t;

/**
 * @struct full_position_t
 * @brief Represents the full position of an entity in the source code.
 *
 * This structure contains the complete position information for an entity in the
 * source code, including the file name, line, column, and a pointer to the
 * corresponding source text. It allows precise error reporting and debugging by
 * providing the exact location of the entity and access to the source text at
 * that point.
 */
typedef struct {
    /**
     * @brief The name of the source file.
     */
    const char *file_name;

    /**
     * @brief The starting row (line) number of the entity.
     */
    size_t row;

    /**
     * @brief The starting column number of the entity.
     */
    size_t column;

    /**
     * @brief Pointer to the source text at the entity position.
     *
     * This field holds a pointer to the exact source text at the entity location.
     */
    const wchar_t *code;

    /**
     * @brief Offset of the entity from the beginning of the file.
     *
     * This field stores the number of characters from the start of the file to
     * the beginning of the entity.
     */
    size_t offset;
} full_position_t;

/**
 * @struct short_position_t
 * @brief Represents the shortened position of an entity in the source code.
 *
 * This structure contains only the line number, column number, and offset of an
 * entity position, without file name or source text pointer.
 */
typedef struct {
    /**
     * @brief The row (line) number of the entity.
     */
    size_t row;

    /**
     * @brief The column number of the entity.
     */
    size_t column;

    /**
     * @brief Offset of the entity from the beginning of the file.
     */
    size_t offset;
} short_position_t;

/**
 * @struct position_range_t
 * @brief Represents a source range occupied by an entity.
 *
 * This structure stores the beginning and ending positions of an entity in the
 * source code. The beginning is stored as a full position, while the end is
 * stored as a shortened position.
 */
typedef struct {
    /**
     * @brief Full position of the beginning of the range.
     */
    full_position_t *begin;

    /**
     * @brief Shortened position of the end of the range.
     */
    short_position_t *end;
} position_range_t;

/**
 * @brief Copies a full position into the specified memory arena.
 *
 * Allocates memory for a new @ref full_position_t in the given arena and copies
 * the contents of the source position into it.
 *
 * @param arena Memory arena used for allocation.
 * @param position Pointer to the source full position to copy.
 * @return Pointer to the copied full position in arena memory, or `NULL` if
 *  `position` is `NULL`.
 */
full_position_t *copy_full_position_to_arena(arena_t *arena, const full_position_t *position);

/**
 * @brief Creates a shortened copy of a full position in the specified memory arena.
 *
 * Allocates memory for a new @ref short_position_t in the given arena and fills
 * it with the row, column, and offset values taken from the supplied full
 * position.
 *
 * @param arena Memory arena used for allocation.
 * @param position Pointer to the source full position.
 * @return Pointer to the created short position in arena memory, or `NULL` if
 *  `position` is `NULL`.
 */
short_position_t *create_short_position_from_full(arena_t *arena, const full_position_t *position);

/**
 * @brief Creates a new source range in the specified memory arena.
 *
 * Allocates memory for a new @ref position_range_t in the given arena and stores
 * the supplied beginning and ending positions in it.
 *
 * @param arena Memory arena used for allocation.
 * @param begin Pointer to the full beginning position of the range.
 * @param end Pointer to the shortened ending position of the range.
 * @return Pointer to the created range in arena memory.
 */
position_range_t *create_position_range(arena_t *arena, full_position_t *begin,
        short_position_t *end);
