/**
 * @file value.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines structures and unions for storing different types of values in a unified way.
 *
 * This file contains definitions for various structures and unions that allow for flexible
 * and efficient storage of different data types, including primitive types like integers and
 * pointers, as well as specialized structures for specific types of data (such as integers with
 * presence flags).
 * 
 * The goal is to provide a set of types that can be used in a unified collection to represent
 * a wide range of values, while keeping the data representation compact and manageable.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

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

/**
 * @struct int_value_t
 * @brief Structure to represent an integer value with a presence flag.
 * 
 * This structure is used to represent an integer value, along with a flag indicating whether
 * the value is present or not. It is useful for handling cases where an object may or may not have
 * a valid integer value, allowing for a more flexible and explicit representation of the data.
 */
typedef struct {
    /**
     * @brief Flag indicating whether the integer value is present.
     * 
     * This boolean flag is used to signify whether the `value` field contains a valid integer.
     * If `has_value` is `false`, the `value` field should be considered invalid.
     */
    bool has_value;

    /**
     * @brief The integer value.
     * 
     * This field holds the integer value of the object, if `has_value` is `true`. 
     * If `has_value` is `false`, the `value` field should be ignored or treated as invalid.
     */
    int64_t value;
} int_value_t;

/**
 * @struct string_value_t
 * @brief Structure to represent a string value with a memory management flag.
 * 
 * This structure is used to represent a string value, along with a flag indicating whether the
 * calling method is responsible for freeing the allocated memory.
 * If the `wstr` field is `NULL`, it indicates that the object does not have a valid string value.
 */
typedef struct {
    /**
     * @brief A pointer to the string value.
     * 
     * This field holds a pointer to a wide-character string (`wchar_t*`). If the string is `NULL`, 
     * it indicates the absence of a valid string value.
     */
    wchar_t *wstr;

    /**
     * @brief A flag indicating whether the caller is responsible for freeing the allocated memory.
     * 
     * If this flag is set to `true`, the caller must free the string memory using `FREE` when
     * it is no longer needed. If the flag is `false`, the string memory is managed elsewhere,
     * and the caller should not free it.
     */
    bool should_free;
} string_value_t;
