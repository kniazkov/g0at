/**
 * @file string_ext.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Extension of the standard C library for working with strings.
 */

#pragma once

#include "value.h"

#include <stdarg.h>
#include <string.h>
#include <wchar.h>

/** @brief Returns an owned copy; NULL input produces an empty string. Free with FREE(). */
wchar_t *WSTRDUP(const wchar_t *wstr);

/**
 * @brief Comparator function for wide strings (wchar_t*).
 * @return A negative value if `first` is less than `second`, zero if they are equal, and a positive
 * value if `first` is greater than `second`.
 */
int string_comparator(const void *first, const void *second);

/**
 * @brief Buffer shared with the string returned by append operations.
 *
 * Further appends may invalidate earlier results. Once the final string is used, stop
 * using the builder and release the string once with FREE_STRING(). No separate
 * transfer or builder destruction is needed. Do not free intermediate results.
 * Allocated buffers are null-terminated; an unallocated empty builder has data == NULL.
 */
typedef struct {
    wchar_t *data;   /**< Pointer to the dynamically allocated wide-character string buffer. */
    size_t length;   /**< Current length of the string, excluding the null terminator. */
    size_t capacity; /**< Total capacity of the allocated buffer. */
} string_builder_t;

/** @brief Initializes an empty builder; zero capacity defers allocation. */
void init_string_builder(string_builder_t *builder, size_t capacity);

/**
 * @brief Reserves at least new_capacity characters, excluding the terminator.
 * Growth is approximately 1.5x; requests within capacity do nothing. May invalidate earlier
 * results.
 */
void resize_string_builder(string_builder_t *builder, size_t new_capacity);

/** @brief Appends a single wide character to the string builder. */
string_value_t append_char(string_builder_t *builder, wchar_t symbol);

/** @brief Appends a substring to the string builder. */
string_value_t append_substring(string_builder_t *builder, const wchar_t *wstr, size_t wstr_length);

/** @brief Appends a string value to the string builder. */
string_value_t append_string_value(string_builder_t *builder, string_value_t value);

/** @brief Appends a string view to the string builder. */
string_value_t append_string_view(string_builder_t *builder, string_view_t view);

/** @brief Appends a wide-character string to the string builder. */
string_value_t append_string(string_builder_t *builder, const wchar_t *wstr);

/** @brief Appends a static wide string literal to the string builder. */
#define append_static_string(builder, str)                                                         \
    append_substring(builder, (str), sizeof(str) / sizeof(wchar_t) - 1)

/** @brief Appends ASCII bytes widened to wchar_t; does not decode UTF-8. */
string_value_t append_ascii_string(string_builder_t *builder, const char *str);

/** @brief Appends a specified number of identical wide characters to the string builder. */
string_value_t append_repeated_char(string_builder_t *builder, wchar_t symbol, size_t count);

/**
 * @brief Returns an owned UTF-8 string; release it with FREE().
 * Input is UTF-16 on 16-bit wchar_t platforms, Unicode scalars otherwise.
 * Invalid input units are replaced with U+FFFD.
 */
char *encode_utf8(const wchar_t *wstr);

/**
 * @brief Like encode_utf8(), also returns the byte count excluding the terminator.
 * `size_ptr`: Optional output; may be NULL.
 */
char *encode_utf8_ex(const wchar_t *wstr, size_t *size_ptr);

/**
 * @brief Decodes UTF-8; release the result with FREE_STRING().
 * Rejects overlong encodings, surrogates, truncated sequences, and values above U+10FFFF
 * with NULL_STRING_VALUE. Empty input returns EMPTY_STRING_VALUE without allocation.
 * On 16-bit wchar_t platforms, supplementary characters become surrogate pairs.
 * The length counts wchar_t units, not Unicode characters.
 */
string_value_t decode_utf8(const char *str);

/** @brief Converts a string to its notation representation with escape sequences. */
string_value_t string_to_string_notation(const wchar_t *prefix, const string_value_t str);

/**
 * @brief Formats a double for display.
 * Uses 11 fractional digits for magnitudes in [1e-10, 1e10], trimming trailing zeros
 * but keeping one fractional digit; otherwise uses %g. Output is bounded by buffer_size.
 */
void double_to_string(double value, char *buffer, size_t buffer_size);

/**
 * @brief Formats text; release the result with FREE_STRING().
 * Supports %%, %c, %s (wide), %a (ASCII), %d/%i, %u, %ld/%li (int64_t), %zu, and %f.
 */
string_value_t format_string_vargs(const wchar_t *format, va_list args);

/** @brief Variadic wrapper for format_string_vargs(). */
static inline string_value_t format_string(const wchar_t *format, ...) {
    va_list args;
    va_start(args, format);
    string_value_t value = format_string_vargs(format, args);
    va_end(args);
    return value;
}

/** @brief Padding direction for fixed-width text. */
typedef enum {
    ALIGN_LEFT,   /**< Align text to the left (padding added to the right). */
    ALIGN_CENTER, /**< Center text within the given width (padding on both sides). */
    ALIGN_RIGHT   /**< Align text to the right (padding added to the left). */
} alignment_t;

/** @brief Pads or truncates to size wchar_t units; release with FREE_STRING(). */
string_value_t align_text(string_value_t text, size_t size, alignment_t alignment);
