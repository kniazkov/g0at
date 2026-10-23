/** @file integer_range.h
 * @brief Shared integer-range reduction used by the lattice and module metadata.
 */
#pragma once

#include <stdint.h>

/** Canonical shape of an inclusive int64 range. */
typedef enum {
    GOAT_RANGE_INVALID,
    GOAT_RANGE_CONSTANT,
    GOAT_RANGE_FULL,
    GOAT_RANGE_BOUNDED
} goat_integer_range_kind;

/** Classify without allocation or arithmetic overflow; invalid input stays invalid. */
static inline goat_integer_range_kind goat_classify_integer_range(int64_t min, int64_t max) {
    if (min > max)
        return GOAT_RANGE_INVALID;
    if (min == max)
        return GOAT_RANGE_CONSTANT;
    if (min == INT64_MIN && max == INT64_MAX)
        return GOAT_RANGE_FULL;
    return GOAT_RANGE_BOUNDED;
}
