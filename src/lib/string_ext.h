/**
 * @file string_ext.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Extension of the standard C library for working with strings.
 */

#pragma once

#include "goat/unicode.h"
#include "number_format.h"
#include "value.h"

#include <stdarg.h>
#include <string.h>

/** @brief Returns an owned copy; NULL input produces an empty string. Free with FREE(). */
goat_char_t *WSTRDUP(const goat_char_t *wstr);

/**
 * @brief Comparator function for wide strings (goat_char_t*).
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
    goat_char_t *data; /**< Pointer to the dynamically allocated wide-character string buffer. */
    size_t length;     /**< Current length of the string, excluding the null terminator. */
    size_t capacity;   /**< Total capacity of the allocated buffer. */
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
string_value_t append_char(string_builder_t *builder, goat_char_t symbol);

/** @brief Appends a substring to the string builder. */
string_value_t
append_substring(string_builder_t *builder, const goat_char_t *wstr, size_t wstr_length);

/** @brief Appends a string value to the string builder. */
string_value_t append_string_value(string_builder_t *builder, string_value_t value);

/** @brief Appends a string view to the string builder. */
string_value_t append_string_view(string_builder_t *builder, string_view_t view);

/** @brief Appends a wide-character string to the string builder. */
string_value_t append_string(string_builder_t *builder, const goat_char_t *wstr);

/** @brief Appends a static wide string literal to the string builder. */
#define append_static_string(builder, str)                                                         \
    append_substring(builder, (str), sizeof(str) / sizeof(goat_char_t) - 1)

/** @brief Appends ASCII bytes widened to goat_char_t; does not decode UTF-8. */
string_value_t append_ascii_string(string_builder_t *builder, const char *str);

/** @brief Appends a specified number of identical wide characters to the string builder. */
string_value_t append_repeated_char(string_builder_t *builder, goat_char_t symbol, size_t count);

/**
 * @brief Returns an owned UTF-8 string; release it with FREE().
 * Terminated-input convenience API for identifiers. Invalid scalars return NULL.
 * General string values must use encode_utf8_view() to preserve embedded zeros.
 */
char *encode_utf8(const goat_char_t *wstr);

/**
 * @brief Like encode_utf8(), also returns the byte count excluding the terminator.
 * `size_ptr`: Optional output; may be NULL.
 */
char *encode_utf8_ex(const goat_char_t *wstr, size_t *size_ptr);

/**
 * @brief Decodes UTF-8; release the result with FREE_STRING().
 * Rejects overlong encodings, surrogates, truncated sequences, and values above U+10FFFF
 * with NULL_STRING_VALUE. Empty input returns EMPTY_STRING_VALUE without allocation.
 * The length counts Unicode scalars. Use decode_utf8_n() for explicit byte ranges.
 */
string_value_t decode_utf8(const char *str);

/** @brief Converts a string to its notation representation with escape sequences. */
string_value_t string_to_string_notation(const goat_char_t *prefix, const string_value_t str);

/**
 * @brief Formats text; release the result with FREE_STRING().
 * Supports %%, %c, %s (wide), %a (ASCII), %d/%i, %u, %ld/%li (int64_t), %zu, and %f.
 */
string_value_t format_string_vargs(const goat_char_t *format, va_list args);

/** @brief Variadic wrapper for format_string_vargs(). */
static inline string_value_t format_string(const goat_char_t *format, ...) {
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

/** @brief Pads or truncates to size goat_char_t units; release with FREE_STRING(). */
string_value_t align_text(string_value_t text, size_t size, alignment_t alignment);

/** @brief Compatibility no-op: Unicode operations never change or consult the locale. */
void init_unicode_locale(void);
/** @brief Unicode 17.0.0 simple uppercase mapping, one scalar to one scalar. */
goat_char_t unicode_towupper(goat_char_t ch);
/** @brief Unicode 17.0.0 simple lowercase mapping, one scalar to one scalar. */
goat_char_t unicode_towlower(goat_char_t ch);

/** @brief Strict UTF-8 encoding of the entire explicit range; NULL on invalid scalars. */
char *encode_utf8_view(string_view_t input, size_t *size_ptr);
/** @brief Strict UTF-8 decoding, preserving embedded zeros. */
string_value_t decode_utf8_n(const char *input, size_t length);
/** @brief Full Unicode 17.0.0 default casing; owned result, or empty static value. */
string_value_t unicode_string_case(string_view_t value, bool upper);
/** @brief Bounded formatting into a scalar buffer. */
int goat_snprintf(goat_char_t *out, size_t size, const goat_char_t *format, ...);
/** @brief Parses the ASCII numeric grammar from scalar input. */
double goat_strtod(const goat_char_t *input, goat_char_t **end);

/** @brief Compares whole ranges, including embedded zeros. */
static inline bool string_view_equals(string_view_t a, string_view_t b) {
    return (a.data == NULL) == (b.data == NULL) && a.length == b.length
           && !goat_memcmp(a.data, b.data, a.length);
}

/** @brief Stable FNV-1a hash of UTF-32LE bytes, including all embedded zeros. */
uint64_t string_view_hash(string_view_t value);
/** @brief Strict UTF-8 for a terminated OS boundary; rejects embedded zeros. */
char *encode_utf8_path(string_view_t path);

/** @brief Formats the full range; %v consumes a string_view_t, %s a terminated identifier. */
string_value_t format_string_view_vargs(string_view_t format, va_list args);
/** @brief Variadic explicit-length formatting. */
string_value_t format_string_view(string_view_t format, ...);
/** @brief Strict UTF-16 boundary decoding; preserves zeros; rejects unpaired surrogates. */
string_value_t decode_utf16(const uint16_t *input, size_t length);
/** @brief Strict UTF-16 boundary encoding; FREE() result; length excludes the terminator. */
uint16_t *encode_utf16(string_view_t input, size_t *length);
