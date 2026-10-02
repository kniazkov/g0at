/**
 * @file io.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definitions of structures and function prototypes for input-output operations.
 */

#pragma once

#include "value.h"

#include <stdbool.h>
#include <wchar.h>

/**
 * @brief Initializes the input-output system.
 * @return `true` if initialization is successful, `false` otherwise.
 */
bool init_io(void);

/**
 * @brief Reads and decodes a file; release the result with FREE_STRING().
 * Returns NULL_STRING_VALUE on file or decoding errors, EMPTY_STRING_VALUE for an empty file.
 */
string_value_t read_utf8_file(const char *filename);

/** @brief Writes UTF-8 text; returns false on open, write, or close failure. */
bool write_utf8_file(const char *filename, const wchar_t *content);

/** @brief Writes UTF-8 text to stdout. */
void print_utf8(const wchar_t *content);

/**
 * @brief Writes UTF-8 text using format_string_vargs() formatting rules.
 * Percent signs in substituted values are written literally.
 */
void fprintf_utf8(FILE *file, const wchar_t *format, ...);

/** @brief GPIO input stub; returns false. */
bool read_digital_input(int index);

/** @brief GPIO output stub; does nothing. */
void write_digital_output(int index, bool value);

/** @brief Reads one line without LF/CRLF; EOF is empty, errors return NULL. Caller frees. */
string_value_t read_input_line(FILE *file);
