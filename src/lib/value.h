/**
 * @file value.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines structures and unions for storing different types of values in a unified way.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

/** @brief A union for storing different primitive types in a single collection element. */
typedef union {
    void *ptr;           /**< A void pointer, capable of pointing to any type of data. */
    uint32_t uint32_val; /**< An unsigned 32-bit integer. */
} value_t;

/** @brief An integer value with a presence flag. */
typedef struct {
    /** @brief Flag indicating whether the integer value is present. */
    bool has_value;

    /** @brief The integer value. */
    int64_t value;
} int_value_t;

/** @brief A floating-point (real) value with a presence flag. */
typedef struct {
    /** @brief Flag indicating whether the real value is present. */
    bool has_value;

    /** @brief The floating-point value. */
    double value;
} real_value_t;

/**
 * @brief Immutable string result with conditional ownership.
 * Release with FREE_STRING(); should_free identifies heap-owned storage.
 * @invariant NULL data implies zero length.
 */
typedef struct {
    /**
     * @brief Pointer to immutable string data (or NULL).
     * @warning Never modify through this pointer - use `string_builder_t` for mutation.
     */
    const wchar_t *data;

    /** @brief Length in wchar_t units, excluding the terminator. */
    size_t length;

    /**
     * @brief Ownership flag
     * @warning Caller must use `FREE_STRING()` when done
     */
    bool should_free;
} string_value_t;

/** @brief Wraps a wide string literal without taking ownership. */
#define STATIC_STRING(str)                                                                         \
    (string_value_t) {                                                                             \
        .data = (str), .length = sizeof(str) / sizeof(wchar_t) - 1, .should_free = false           \
    }

/** @brief Macro to create empty string value. */
#define EMPTY_STRING_VALUE                                                                         \
    (string_value_t) {                                                                             \
        L"", 0, false                                                                              \
    }

/** @brief Macro for creating a null (non-existing) string value. */
#define NULL_STRING_VALUE                                                                          \
    (string_value_t) {                                                                             \
        NULL, 0, false                                                                             \
    }

/** @brief Macro to clear the memory of a string if it needs to be cleared. */
#define FREE_STRING(v)                                                                             \
    if ((v).should_free)                                                                           \
    FREE((wchar_t *)((v).data))

/**
 * @brief Borrowed string; its backing storage must outlive the view.
 * @invariant Non-NULL data is null-terminated; NULL data implies zero length.
 */
typedef struct {
    /**
     * @brief Pointer to immutable string data (or NULL).
     * @note Unlike string_value_t, views never own their data.
     */
    const wchar_t *data;

    /** @brief Precomputed length (excluding null terminator). */
    size_t length;
} string_view_t;

/** @brief Macro to create empty string view. */
#define EMPTY_STRING_VIEW                                                                          \
    (string_view_t) {                                                                              \
        L"", 0                                                                                     \
    }

/** @brief Borrows a view without transferring ownership. */
#define VALUE_TO_VIEW(v) ((string_view_t){.data = (v).data, .length = (v).length})

/** @brief Wraps a view as a non-owning string value. */
#define VIEW_TO_VALUE(v)                                                                           \
    ((string_value_t){.data = (v).data, .length = (v).length, .should_free = false})
