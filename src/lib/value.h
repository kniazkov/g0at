/**
 * @file value.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines a union for storing different primitive types in a single collection element.
 */

#pragma once

#include <stdint.h>

/**
 * @union value_t
 * @brief A union for storing different primitive types in a single collection element.
 *
 * This union can hold two types of data:
 * - A `void*` pointer, which can point to any type of data.
 * - An unsigned 32-bit integer, which can store small integer values.
 */
typedef union {
    void *ptr; /**< A void pointer, capable of pointing to any type of data. */
    uint32_t uint32_val; /**< An unsigned 32-bit integer. */
} value_t;
