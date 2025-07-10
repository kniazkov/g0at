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

#include <stdbool.h>
#include <wchar.h>

#include "value.h"

/**
 * @brief Initializes the input-output system.
 * 
 * This function initializes the input-output system by setting up platform-specific components, 
 * such as GPIO libraries or configurations. On platforms with GPIO support, it will initialize 
 * the necessary libraries. This function should be called at the beginning of the application
 * to ensure proper initialization of the I/O system.
 * 
 * @return `true` if initialization is successful, `false` otherwise.
 */
bool init_io(void);

/**
 * @brief Reads a UTF-8 encoded file and returns the decoded string.
 * 
 * This function opens a UTF-8 encoded file, reads its contents into a buffer, and decodes the 
 * buffer into a wide-character string (`wchar_t*`). The result is returned as a `string_value_t` 
 * structure, which contains the decoded string, its length, and a flag indicating whether the 
 * calling method is responsible for freeing the allocated memory.
 * 
 * If the file cannot be opened, read, or decoded, the returned structure will contain a `NULL` 
 * string and a length of `0`. If the file contains valid UTF-8 data, the returned structure will 
 * contain the decoded string and its length.
 * 
 * @param filename The name of the file to read (UTF-8 encoded).
 * @return A `string_value_t` structure containing the decoded wide-character string, its length, 
 *  and a flag indicating whether the memory should be freed by the caller. If the function fails
 *  to read or decode the file, the structure will contain a `NULL` string.
 * 
 * @note This function does not check the validity of the UTF-8 content in detail — if the content
 *  is invalid, it will return a `NULL` string, and the `should_free` flag will be `false`.
 */
string_value_t read_utf8_file(const char *filename);

/**
 * @brief Writes a wide-character string to a file as UTF-8 encoded text.
 * 
 * This function converts the provided wide-character string (`wchar_t*`) to UTF-8 encoding
 * and writes it to the specified file. If the file cannot be opened, if an encoding error occurs,
 * or if any other failure happens during the process, the function returns `false`. 
 * It returns `true` if the writing operation is successful.
 * 
 * @param filename The name of the file to write to (UTF-8 encoded).
 * @param content The wide-character string to be written to the file.
 * @return `true` if the file was written successfully, `false` if an error occurred.
 */
bool write_utf8_file(const char *filename, const wchar_t *content);

/**
 * @brief Prints a wide-character string to the standard output as UTF-8 encoded text.
 * 
 * This function converts the provided wide-character string (`wchar_t*`) to UTF-8 encoding
 * and prints it to the standard output (console). It ensures that the output is correctly encoded
 * in UTF-8, which is widely supported for modern consoles and terminals.
 * 
 * @param content The wide-character string to be printed.
 */
void print_utf8(const wchar_t *content);

/**
 * @brief Prints a formatted wide-character string to a specified file as UTF-8 encoded text.
 * 
 * This function formats a wide-character string using a subset of `printf`-style format specifiers 
 * and prints the resulting text to the specified file stream as UTF-8 encoded text. It combines 
 * the functionality of `format_string` for formatting and UTF-8 encoding with file output.
 * 
 * The function supports the following format specifiers:
 * - **`%c`**: Inserts a single wide character (`wchar_t`).
 * - **`%s`**: Inserts a wide-character string (`wchar_t*`).
 * - **`%d`, `%i`**: Inserts a signed integer (`int`).
 * - **`%u`**: Inserts an unsigned integer (`unsigned int`).
 * - **`%zu`**: Inserts an unsigned size (`size_t`).
 * - **`%ld`, `%li`**: Inserts a 64-bit signed integer (`int64_t`).
 * - **`%f`**: Inserts a double, formatted according to `double_to_string`.
 * - **`%%`**: Inserts a literal `%`.
 * 
 * @param file The file stream where the formatted string should be printed
 *  (e.g., `stdout` or `stderr`).
 * @param format The wide-character format string containing placeholders for the arguments.
 * @param ... The variable arguments to substitute into the format string.
 */
void fprintf_utf8(FILE *file, const wchar_t *format, ...);

/**
 * @brief Reads a digital input from a specified GPIO pin.
 * 
 * This function reads a digital input (either `0` or `1`) from the specified GPIO pin (index).
 * The behavior of this function depends on the platform and implementation details.
 * On supported platforms, the function will read the current state of the GPIO pin (e.g., 
 * from a Raspberry Pi or similar hardware). The implementation should be provided under 
 * specific platform flags (e.g., `#ifdef RASPBERRY_PI`).
 * 
 * @param index The index of the GPIO pin to read from.
 * @return The digital value of the GPIO pin.
 */
bool read_digital_input(int index);

/**
 * @brief Writes a digital output to a specified GPIO pin.
 * 
 * This function writes a digital output (`0` or `1`) to the specified GPIO pin (index).
 * The behavior of this function depends on the platform and implementation details.
 * On supported platforms, the function will set the state of the GPIO pin (e.g., 
 * on a Raspberry Pi or similar hardware). The implementation should be provided under 
 * specific platform flags (e.g., `#ifdef RASPBERRY_PI`).
 * 
 * @param index The index of the GPIO pin to write to.
 * @param value The digital value to write.
 */
void write_digital_output(int index, bool value);
