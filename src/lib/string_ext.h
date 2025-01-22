/**
 * @file string_ext.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Extension of the standard C library for working with strings.
 */

#pragma once

#include <string.h>
#include <wchar.h>

#include "value.h"

/**
 * @brief Duplicates a wide character string.
 * 
 * This function allocates memory for a copy of the given wide character string 
 * (including the null terminator). If the input string is NULL, a new empty 
 * string (i.e., a single null terminator) is returned. 
 * 
 * @param wstr The wide character string to duplicate.
 * @return A pointer to the newly allocated wide character string. 
 *  If the input string is NULL, an empty string is returned.
 */
wchar_t *WSTRDUP(const wchar_t *wstr);

/**
 * @brief Comparator function for wide strings (wchar_t*).
 * 
 * This function compares two wide-character strings (`wchar_t *`) using the `wcscmp` function. 
 * It can be used as a comparator in any algorithm or data structure that requires ordering 
 * or comparison logic, such as AVL trees, sorting algorithms, or hash maps.
 * 
 * @param first A pointer to the first wide-character string.
 * @param second A pointer to the second wide-character string.
 * 
 * @return A negative value if `first` is less than `second`, 
 *  zero if they are equal, and a positive value if `first` is greater than `second`.
 */
int string_comparator(const void *first, const void *second);

/**
 * @brief Represents a dynamic string builder for wide-character strings.
 * 
 * This structure allows constructing strings by dynamically resizing an internal buffer as needed.
 * The string is always null-terminated and can be retrieved by the user.
 */
typedef struct {
    wchar_t *data;      /**< Pointer to the dynamically allocated wide-character string buffer. */
    size_t length;      /**< Current length of the string, excluding the null terminator. */
    size_t capacity;    /**< Total capacity of the allocated buffer. */
} string_builder_t;

/**
 * @brief Initializes the string builder with a specified initial capacity.
 * 
 * This function initializes a string builder instance, setting the internal fields to
 * represent an empty string. It allocates an internal buffer with the specified capacity.
 * If the provided capacity is zero, no memory is allocated initially, and the builder
 * will need to grow the buffer as needed when data is appended.
 * 
 * @param builder A pointer to the `string_builder_t` instance to initialize.
 * @param capacity The initial capacity of the internal buffer. If set to zero, no memory
 *  is allocated initially.
 */
void init_string_builder(string_builder_t *builder, size_t capacity);

/**
 * @brief Resizes the internal buffer of the string builder to a new capacity.
 * 
 * If the new capacity is less than or equal to the current capacity, no changes are made.
 * The data in the current buffer is preserved and copied into the new buffer.
 * 
 * @param builder A pointer to the string_builder_t instance.
 * @param new_capacity The new capacity for the buffer.
 */
void resize_string_builder(string_builder_t *builder, size_t new_capacity);

/**
 * @brief Appends a single wide character to the string builder.
 * 
 * Adds the specified wide character to the end of the string, resizing the buffer if necessary.
 * The string remains null-terminated after the operation.
 * 
 * @param builder A pointer to the string_builder_t instance.
 * @param symbol The wide character to append.
 * @return A `string_value_t` structure containing the updated string buffer and its length.
 */
string_value_t append_char(string_builder_t *builder, wchar_t symbol);

/**
 * @brief Appends a substring to the string builder.
 * 
 * Adds a wide-character substring (of length `wstr_length`) to the end of the builder's string,
 * resizing the buffer if necessary. The string remains null-terminated after the operation.
 * 
 * @param builder A pointer to the string_builder_t instance.
 * @param wstr A pointer to the wide-character string to append.
 * @param wstr_length The length of the substring to append.
 * @return A `string_value_t` structure containing the updated string buffer and its length.
 */
string_value_t append_substring(string_builder_t *builder, const wchar_t *wstr, size_t wstr_length);

/**
 * @brief Appends a wide-character string to the string builder.
 * 
 * Adds the specified wide-character string to the end of the builder's string,
 * resizing the buffer if necessary. The string remains null-terminated after the operation.
 * 
 * @param builder A pointer to the string_builder_t instance.
 * @param wstr A pointer to the wide-character string to append.
 * @return A `string_value_t` structure containing the updated string buffer and its length.
 */
string_value_t append_string(string_builder_t *builder, const wchar_t *wstr);

/**
 * @brief Encodes a wide-character string (`wchar_t*`) into a UTF-8 encoded string.
 * 
 * This function takes a wide-character string (`wchar_t*`) and encodes it into a UTF-8
 * encoded string. It processes each wide-character symbol in the input string and converts it
 * to the corresponding UTF-8 bytes. The resulting string is dynamically allocated,
 * and it is null-terminated.
 * 
 * @param wstr The wide-character string (`wchar_t*`) to encode.
 * @return A dynamically allocated UTF-8 encoded string (`char*`).
 * @note The caller is responsible for freeing the memory allocated for the UTF-8 encoded string.
 */
char *encode_utf8(const wchar_t *wstr);

/**
 * @brief Encodes a wide-character string (`wchar_t*`) into a UTF-8 encoded string
 *  and returns its size.
 * 
 * This function takes a wide-character string (`wchar_t*`) and encodes it into a UTF-8
 * encoded string. The resulting UTF-8 string is dynamically allocated and null-terminated.
 * Additionally, the size of the encoded string (in bytes) is returned via the `size_ptr`
 * parameter, allowing the caller to avoid recalculating the size when it is needed immediately.
 * 
 * @param wstr The wide-character string (`wchar_t*`) to encode.
 * @param size_ptr A pointer to a `size_t` variable where the size of the UTF-8 encoded string
 *  (in bytes) will be stored.
 * @return A dynamically allocated UTF-8 encoded string (`char*`).
 * @note The caller must free the memory allocated for the UTF-8 encoded string after use.
 */
char *encode_utf8_ex(const wchar_t *wstr, size_t *size_ptr);

/**
 * @brief Decodes a UTF-8 encoded string into a wide-character string.
 * 
 * This function decodes a UTF-8 encoded string (`char*`) into a wide-character string (`wchar_t*`).
 * The function processes the string byte-by-byte and handles characters encoded with 1 to 4 bytes, 
 * according to the UTF-8 encoding scheme. If any invalid byte sequences are detected, the function
 * will return an empty result (i.e., a failed string).
 * 
 * @param str A pointer to the UTF-8 encoded string (`char*`) to decode.
 * @return A `string_value_t` structure containing the decoded wide-character string.
 * @note The caller is responsible for managing the memory of the decoded string.
 */
string_value_t decode_utf8(const char *str);

/**
 * @brief Converts a string to its notation representation with escape sequences.
 * 
 * This function converts the given string to its string notation representation by escaping
 * special characters (such as newline, carriage return, tab, quotes, and backslashes) and
 * surrounding the string with double quotes. It also prepends a given prefix to the string, 
 * which can be used for code generation in different programming languages.
 * 
 * For example, when generating code in C, the prefix `L` may be used to generate wide string
 * literals like `L"hello"`.
 * 
 * @param prefix A prefix string to prepend to the string notation.
 * @param str The original string value to convert to its string notation.
 * @return A `string_value_t` structure containing the escaped string representation.
 */
string_value_t string_to_string_notation(const wchar_t *prefix, const string_value_t str);

/**
 * @brief Converts a double value to a string representation with specific formatting rules.
 * 
 * This function formats a `double` value as a string, adhering to the following rules:
 * - For values within the range \([10^{-10}, 10^{10}]\) (inclusive), the function:
 *   - Displays up to 15 digits after the decimal point if necessary.
 *   - Removes trailing zeros after the decimal point but ensures at least one digit remains 
 *     after the decimal (e.g., `1.0` instead of `1`).
 * - For values outside this range, the function uses scientific (exponential) notation with
 *   up to 15 significant digits (e.g., `1.024e+11`).
 * 
 * The resulting string is written to the provided buffer, ensuring that it does not exceed
 * the specified buffer size.
 * 
 * @param value The double value to convert to a string.
 * @param buffer A pointer to the buffer where the resulting string will be stored.
 * @param buffer_size The size of the provided buffer in bytes. The function ensures that 
 *  the output string does not exceed this size, including the null terminator.
 */
void double_to_string(double value, char *buffer, size_t buffer_size);
