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
 * @struct real_value_t
 * @brief Represents a floating-point (real) value with a presence flag.
 * 
 * This structure is used to represent a double-precision floating-point value, along with 
 * a flag indicating whether the value is present or not. It provides a flexible way to handle 
 * cases where an object may or may not have a valid real value.
 */
typedef struct {
    /**
     * @brief Flag indicating whether the real value is present.
     * 
     * This boolean flag signifies whether the `value` field contains a valid floating-point number.
     * If `has_value` is `false`, the `value` field should be considered invalid.
     */
    bool has_value;

    /**
     * @brief The floating-point value.
     * 
     * This field holds the double-precision floating-point value of the object if `has_value` 
     * is `true`. If `has_value` is `false`, the `value` field should be ignored.
     */
    double value;
} real_value_t;

/**
 * @struct string_value_t
 * @brief Structure to represent a string value with a memory management flag and length.
 * 
 * This structure is used to represent a string value, along with a flag indicating whether the
 * calling method is responsible for freeing the allocated memory. It also stores the length
 * of the string for convenience, so the caller doesn't need to calculate it every time.
 * If the `wstr` field is `NULL`, it indicates that the object does not have a valid string value.
 */
typedef struct {
    /**
     * @brief A pointer to the string value.
     * 
     * This field holds a pointer to a wide-character string (`wchar_t*`). If the string is `NULL`, 
     * it indicates the absence of a valid string value.
     */
    wchar_t *data;

    /**
     * @brief The length of the string.
     * 
     * This field holds the length of the string (number of characters). It is useful to avoid 
     * recomputing the length each time and can be used for efficient string manipulation.
     * If `data` is `NULL`, `length` is typically zero.
     */
    size_t length;

    /**
     * @brief A flag indicating whether the caller is responsible for freeing the allocated memory.
     * 
     * If this flag is set to `true`, the caller must free the string memory using `FREE` when
     * it is no longer needed. If the flag is `false`, the string memory is managed elsewhere,
     * and the caller should not free it.
     */
    bool should_free;
} string_value_t;

/**
 * @brief Macro to create a `string_value_t` from a static string literal.
 * 
 * This macro creates a `string_value_t` structure from a wide-character string literal.
 * The resulting structure will have the string's data, its length, and the `should_free` flag
 * set to `false` (indicating that the string memory is managed statically and does not need
 * to be freed).
 * 
 * @param str The wide-character string literal.
 * @return A `string_value_t` structure with the data, length, and `should_free` flag.
 */
#define STATIC_STRING(str) \
    (string_value_t){ \
        .data = (wchar_t *)(str), \
        .length = sizeof(str) / sizeof(wchar_t) - 1, \
        .should_free = false \
    }

/**
 * @struct string_view_t
 * @brief A non-owning view of a null-terminated wide-character string.
 *
 * This structure provides a lightweight way to examine and pass string data without
 * memory ownership.
 * - NULL `data` indicates the absence of a string (no answer, uninitialized field)
 * - Non-NULL `data` always points to a valid null-terminated string (which may be empty)
 */
typedef struct {
    /**
     * @brief Pointer to a null-terminated wide-character string or NULL.
     * 
     * When NULL, indicates the string doesn't exist.
     * When non-NULL, must point to a valid null-terminated string which may be empty.
     */
    wchar_t* data;

    /**
     * @brief Length of the string in characters (excluding null terminator).
     * 
     * When data is NULL, this must be 0. For non-NULL data, this equals wcslen(data).
     * Provides O(1) access to string length without computation.
     */
    size_t length;
} string_view_t;

/**
 * @def STRING_VALUE_TO_VIEW(s)
 * @brief Safely converts a string_value_t to a string_view_t.
 * @param s The string_value_t to convert (must have valid invariants)
 * @return A string_view_t with matching null/non-null semantics
 */
#define STRING_VALUE_TO_VIEW(s) \
    ((string_view_t){ .data = (s).data, .length = (s).data ? (s).length : 0 })

/**
 * @def STRING_VIEW_TO_VALUE(s)
 * @brief Safely converts a string_view_t to a string_value_t.
 * @param sv The string_view_t to convert (must have valid invariants)
 * @return A string_value_t with matching null/non-null semantics and false should_free flag
 */
#define STRING_VIEW_TO_VALUE(s) \
    ((string_value_t){ .data = (s).data, .length = (s).data ? (s).length : 0, .should_free = false })
