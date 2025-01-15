/**
 * @file io.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definitions of structures and function prototypes for input-output operations.
 *
 * This file provides the function prototypes for various input-output operations,
 * including reading and writing to files, as well as handling different types of input.
 * It supports both standard I/O and file I/O in a way that ensures memory safety
 * and error handling. The functions in this file are designed to work with
 * UTF-8 encoded text files and wide-character strings.
 */

#pragma once

#include <wchar.h>

/**
 * @brief Reads the contents of a UTF-8 encoded text file and returns it as a
 *  wide-character string.
 * 
 * This function attempts to read a file with the specified name, decode its contents from UTF-8
 * into a wide-character string (`wchar_t*`), and returns it. If the file cannot be opened,
 * read, or the contents are not valid UTF-8, the function will return NULL.
 * 
 * @param filename The name of the file to be read.
 * @return A wide-character string (`wchar_t*`) containing the decoded content of the file, or
 *  NULL if the file cannot be read or the content is not a valid UTF-8 encoding.
 */
wchar_t* read_utf8_file(const char* filename);
