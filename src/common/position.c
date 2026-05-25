/**
 * @file position.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of helper functions for source positions and ranges.
 */

#include "position.h"
#include "lib/arena.h"

/**
 * @brief Copies a full position into arena memory.
 *
 * @param arena Memory arena used for allocation.
 * @param position Pointer to the source full position.
 * @return Pointer to the copied full position in arena memory, or `NULL` if
 *  `position` is `NULL`.
 */
full_position_t *copy_full_position_to_arena(arena_t *arena, const full_position_t *position) {
    return (full_position_t *)copy_object_to_arena(arena, position, sizeof(full_position_t));
}

/**
 * @brief Creates a shortened position from a full position in arena memory.
 *
 * @param arena Memory arena used for allocation.
 * @param position Pointer to the source full position.
 * @return Pointer to the created short position in arena memory, or `NULL` if
 *  `position` is `NULL`.
 */
short_position_t *create_short_position_from_full(arena_t *arena,
        const full_position_t *position) {
    short_position_t *short_pos = (short_position_t *)alloc_from_arena(
        arena, sizeof(short_position_t));
    short_pos->row = position->row;
    short_pos->column = position->column;
    short_pos->offset = position->offset;
    return short_pos;
}

/**
 * @brief Creates a source range in arena memory.
 *
 * @param arena Memory arena used for allocation.
 * @param begin Pointer to the full beginning position of the range.
 * @param end Pointer to the shortened ending position of the range.
 * @return Pointer to the created range in arena memory.
 */
position_range_t *create_position_range(arena_t *arena, full_position_t *begin,
        short_position_t *end) {
    position_range_t *range = (position_range_t *)alloc_from_arena(
        arena, sizeof(position_range_t));
    range->begin = begin;
    range->end = end;
    return range;
}
