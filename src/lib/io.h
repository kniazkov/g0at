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

/**
 * @brief Initializes the input-output system.
 * 
 * This function initializes the input-output system by setting the locale for handling
 * multi-byte character encodings (UTF-8). On platforms with GPIO support, this function
 * may also initialize necessary libraries or configurations for GPIO access (e.g., WiringPi
 * for Raspberry Pi or Arduino). It should be called at the beginning of the application to 
 * ensure proper initialization.
 * 
 * @return `true` if initialization is successful, `false` otherwise.
 */
bool init_io(void);

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
bool write_utf8_file(const char* filename, const wchar_t* content);

/**
 * @brief Prints a wide-character string to the standard output as UTF-8 encoded text.
 * 
 * This function converts the provided wide-character string (`wchar_t*`) to UTF-8 encoding
 * and prints it to the standard output (console). It ensures that the output is correctly encoded
 * in UTF-8, which is widely supported for modern consoles and terminals.
 * 
 * @param content The wide-character string to be printed.
 */
void print_utf8(const wchar_t* content);

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
