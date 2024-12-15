/**
 * @file string_ext.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Extension of the standard C library for working with strings.
 */

#pragma once

#include <string.h>
#include <wchar.h>

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
 * @brief Initializes the string builder.
 * 
 * Sets the builder's internal fields to represent an empty string with no allocated buffer.
 * 
 * @param builder A pointer to the string_builder_t instance to initialize.
 */
void init_string_builder(string_builder_t *builder);

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
 * @return A pointer to the internal string buffer after the character is appended.
 */
wchar_t *append_char(string_builder_t *builder, wchar_t symbol);

/**
 * @brief Appends a wide-character string to the string builder.
 * 
 * Adds the specified wide-character string to the end of the builder's string,
 * resizing the buffer if necessary. The string remains null-terminated after the operation.
 * 
 * @param builder A pointer to the string_builder_t instance.
 * @param wstr A pointer to the wide-character string to append.
 * @return A pointer to the internal string buffer after the string is appended.
 */
wchar_t *append_string(string_builder_t *builder, wchar_t *wstr);
