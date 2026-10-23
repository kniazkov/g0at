/**
 * @file io.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definitions of structures and function prototypes for input-output operations.
 */

#pragma once

#include "goat/unicode.h"
#include "value.h"

#include <stdbool.h>
#include <stdio.h>

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
bool write_utf8_file(const char *filename, const goat_char_t *content);

/** @brief Writes UTF-8 text to stdout. */
void print_utf8(const goat_char_t *content);

/** @brief Returns true if the cursor on stdout is at the start of a line. */
bool stdout_at_line_start(void);

/** @brief Overrides the tracked stdout line-start state. */
void set_stdout_line_start(bool value);

/** @brief Returns true if standard input is an interactive console. */
bool stdin_is_console(void);

/** @brief Returns true if standard output is an interactive console. */
bool stdout_is_console(void);

/**
 * @brief Writes UTF-8 text using format_string_vargs() formatting rules.
 * Percent signs in substituted values are written literally.
 */
void fprintf_utf8(FILE *file, const goat_char_t *format, ...);

/** @brief GPIO input stub; returns false. */
bool read_digital_input(int index);

/** @brief GPIO output stub; does nothing. */
void write_digital_output(int index, bool value);

/** @brief Reads one line without LF/CRLF; EOF is empty, errors return NULL. Caller frees. */
string_value_t read_input_line(FILE *file);

/** @brief Prints the complete scalar range, including embedded zeros. */
void print_utf8_view(string_view_t value);

/** @brief Reads a UTF-8 file through a checked scalar path; embedded zeros fail before I/O. */
string_value_t read_utf8_file_view(string_view_t filename);
