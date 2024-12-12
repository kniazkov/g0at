/**
 * @file split64.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Provides a union for splitting or combining a 64-bit value.
 * 
 * This is particularly useful in virtual machine implementations, where large values may need
 * to be split into smaller chunks for processing or transmitted across multiple instructions.
 */

#pragma once

#include <stdint.h>

/**
 * @union split64_t
 * @brief Union for splitting or combining a 64-bit value into parts.
 * 
 * This union allows a 64-bit value (`int64_t`) to be represented in different ways:
 * - `int_value`: The 64-bit signed integer part of the 64-bit value.
 * - `real_value`: The 64-bit value as a `double` (floating-point number).
 * - `parts[2]`: An array of two 32-bit unsigned integers representing the high and low
 *   32 bits of the 64-bit value.
 * 
 * This union is particularly useful when working with bytecode instructions that require
 * a 64-bit value to be split into two 32-bit parts or when combining two 32-bit parts into
 * a single 64-bit value.
 */
typedef union {
    /**
     * @brief The 64-bit integer value.
     */
    int64_t int_value;

    /**
     * @brief The 64-bit value represented as a `double` precision floating-point number.
     */
    double real_value;

    /**
     * @brief An array of two 32-bit unsigned integers representing the high and low parts.
     * 
     * This array (`parts[2]`) holds the 64-bit value as two 32-bit unsigned integers:
     * - `parts[0]` for the most significant 32 bits (high part).
     * - `parts[1]` for the least significant 32 bits (low part).
     */
    uint32_t parts[2];  /**< Array for splitting or combining 64-bit values
                             into two 32-bit parts. */
} split64_t;
