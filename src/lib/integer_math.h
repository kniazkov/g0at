/** @file integer_math.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defined arithmetic for signed 64-bit language integers.
 */
#pragma once
#include <stdint.h>
/** @brief Adds modulo 2^64 without signed overflow or out-of-range signed casts. */
static inline int64_t add_int64_wrapping(int64_t left, int64_t right) {
    uint64_t sum = (uint64_t)left + (uint64_t)right;
    return sum <= INT64_MAX ? (int64_t)sum
        : INT64_MIN + (int64_t)(sum - ((uint64_t)INT64_MAX + 1));
}
