/**
 * @file split64.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Provides a union for splitting or combining a 64-bit value.
 */

#pragma once

#include <stdint.h>

/** @brief Union for splitting or combining a 64-bit value into parts. */
typedef union {
    /** @brief The 64-bit integer value. */
    int64_t int_value;

    /** @brief The 64-bit value represented as a `double` precision floating-point number. */
    double real_value;

    uint32_t parts[2]; /**< Array for splitting or combining 64-bit values
                            into two 32-bit parts. */
} split64_t;
