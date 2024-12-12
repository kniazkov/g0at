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
