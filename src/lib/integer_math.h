/** @file integer_math.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defined arithmetic for signed 64-bit language integers.
 */
#pragma once
#include <stdint.h>

/** @brief Adds modulo 2^64 without signed overflow or out-of-range signed casts. */
static inline int64_t add_int64_wrapping(int64_t left, int64_t right) {
    uint64_t sum = (uint64_t)left + (uint64_t)right;
    return sum <= INT64_MAX ? (int64_t)sum : INT64_MIN + (int64_t)(sum - ((uint64_t)INT64_MAX + 1));
}

/** @brief Subtracts modulo 2^64 without signed overflow or negating INT64_MIN. */
static inline int64_t subtract_int64_wrapping(int64_t left, int64_t right) {
    uint64_t difference = (uint64_t)left - (uint64_t)right;
    return difference <= INT64_MAX ? (int64_t)difference
                                   : INT64_MIN + (int64_t)(difference - ((uint64_t)INT64_MAX + 1));
}

/** @brief Multiplies modulo 2^64 without signed overflow. */
static inline int64_t multiply_int64_wrapping(int64_t left, int64_t right) {
    uint64_t product = (uint64_t)left * (uint64_t)right;
    return product <= INT64_MAX ? (int64_t)product
                                : INT64_MIN + (int64_t)(product - ((uint64_t)INT64_MAX + 1));
}

/** @brief Rounds an integer to double before arithmetic, including on x87 targets. */
static inline double integer_to_double(int64_t value) {
    volatile double rounded = (double)value;
    return rounded;
}
