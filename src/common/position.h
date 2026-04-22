/**
 * @file position.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines the structure for representing the position of a token in the source code.
 *
 * This file contains the definition of the `position_t` structure, which is used to store
 * information about the position of a lexeme (or token) within the source code. The position
 * is uniquely defined by the file name, the row (line) number, and the column number, allowing
 * the lexer and other parts of the interpreter to accurately track and report the location of
 * tokens in the input.
 */

#include <stddef.h>

#pragma once

/**
 * @struct full_position_t
 * @brief Represents the full position of a token in the source code.
 * 
 * This structure contains the complete position information for a token in the source code,
 * including the file name, line, column, and a pointer to the token's text in the source code.
 * It allows for precise error reporting and debugging by providing the exact location of the token
 * as well as the ability to access the token's content directly.
 */
typedef struct {
    /**
     * @brief The name of the source file.
     */
    const char *file_name;

    /**
     * @brief The starting row (line) number of the token.
     */
    size_t row;

    /**
     * @brief The starting column number of the token.
     */
    size_t column;

    /**
     * @brief Pointer to the token's text in the source code.
     * 
     * This field holds a pointer to the exact text of the token in the source code,
     * which can differ from its content in the token structure itself.
     */
    const wchar_t const *code;

    /**
     * @brief Offset of the token from the beginning of the file.
     * 
     * This field stores the number of characters from the start of the file to the beginning
     * of the token.
     */
    size_t offset;
} full_position_t;

/**
 * @struct short_position_t
 * @brief Represents the shortened position of a token in the source code.
 * 
 * This structure contains only the line and column numbers of a token's position,
 * without any information about the file or the exact text of the token.
 */
typedef struct {
    /**
     * @brief The row (line) number of the token.
     * 
     * This field stores the line number where the token was found.
     */
    size_t row;

    /**
     * @brief The column number of the token.
     * 
     * This field stores the column number where the token was found.
     */
    size_t column;

    /**
     * @brief Offset of the token from the beginning of the file.
     */
    size_t offset;
} short_position_t;

/**
 * @brief Converts a full position to a short position (only row, column and offset).
 * 
 * This inline function extracts the row and column from a full position structure and
 * creates a `short_position_t` structure with just the row, column and offset information.
 *
 * @param full_pos Pointer to the full position structure.
 * @return A `short_position_t` structure containing only the row, column and offset.
 */
static inline short_position_t full_to_short_position(const full_position_t *full_pos) {
    short_position_t short_pos;
    short_pos.row = full_pos->row;
    short_pos.column = full_pos->column;
    short_pos.offset = full_pos->offset;
    return short_pos;
}
