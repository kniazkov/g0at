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
 * @brief A string with explicit ownership semantics (for function returns).
 *
 * This is the primary type for returning strings from functions, especially when
 * the ownership semantics are unknown at compile time. It solves the "who should
 * free this?" problem by carrying an explicit ownership flag.
 *
 * Key properties:
 * - Immutable content (const) to prevent action-at-a-distance bugs
 * - Length cached for O(1) access
 * - Ownership flag indicates if receiver must FREE_STRING()
 * - Compatible with both static and dynamic strings
 *
 * Usage patterns:
 * - Return from functions that might allocate
 * - Receive strings from virtual methods
 * - Pass through string processing pipelines
 *
 * @invariant If data is NULL, length must be 0.
 * @invariant `should_free == true` implies data was dynamically allocated.
 */
typedef struct {
    /**
     * @brief Pointer to immutable string data (or NULL).
     * @warning Never modify through this pointer - use `string_builder_t` for mutation.
     */
    const wchar_t *data;

    /**
     * @brief Precomputed string length (excluding null terminator).
     * @details Saves O(n) strlen calls in processing pipelines.
     */
    size_t length;

    /**
     * @brief Ownership flag
     * @warning Caller must use `FREE_STRING()` when done
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
        .data = (str), \
        .length = sizeof(str) / sizeof(wchar_t) - 1, \
        .should_free = false \
    }

/**
 * @brief Macro to create empty string value.
 */
#define EMPTY_STRING_VALUE (string_value_t){ L"", 0, false }

/**
 * @brief Macro for creating a null (non-existing) string value.
 */
#define NULL_STRING_VALUE (string_value_t){ NULL, 0, false }

/**
 * @brief Macro to clear the memory of a string if it needs to be cleared.
 */
#define FREE_STRING(v) if ((v).should_free) FREE((wchar_t*)((v).data))

/**
 * @struct string_view_t
 * @brief A non-owning string reference (for function parameters and storage).
 *
 * Lightweight view into a string managed elsewhere. Used when:
 * - The lifetime is clearly managed by other means
 * - Passing strings to functions that shouldn't assume ownership
 * - Storing string references in data structures
 *
 * Design advantages:
 * - No allocation overhead
 * - Works with any string source (static, arena, heap)
 * - Still provides O(1) length access
 *
 * @invariant If data is NULL, length must be 0.
 * @invariant Non-NULL data must point to valid null-terminated string.
 */
typedef struct {
    /**
     * @brief Pointer to immutable string data (or NULL).
     * @note Unlike string_value_t, views never own their data.
     */
    const wchar_t* data;

    /**
     * @brief Precomputed length (excluding null terminator).
     * @details Matches wcslen(data) but computed once.
     */
    size_t length;
} string_view_t;

/**
 * @brief Macro to create empty string view.
 */
#define EMPTY_STRING_VIEW (string_view_t){ L"", 0 }

/**
 * @def VALUE_TO_VIEW(v)
 * @brief Safely converts a string_value_t to a string_view_t.
 * @param v The string_value_t to convert (must have valid invariants)
 * @return A string_view_t with matching null/non-null semantics
 */
#define VALUE_TO_VIEW(v) \
    ((string_view_t){ .data = (v).data, .length = (v).length })

/**
 * @def VIEW_TO_VALUE(v)
 * @brief Safely converts a string_view_t to a string_value_t.
 * @param sv The string_view_t to convert (must have valid invariants)
 * @return A string_value_t with matching null/non-null semantics and false should_free flag
 */
#define VIEW_TO_VALUE(v) \
    ((string_value_t){ .data = (v).data, .length = (v).length, .should_free = false })
