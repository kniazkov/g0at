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
 * @struct position_t
 * @brief Structure to represent the position of a token in the source code.
 * 
 * This structure holds the information necessary to track the position of a token (or lexeme)
 * in the source code. It contains the name of the file, the line (row) number, and the column
 * number, making it possible to pinpoint the exact location of a token in the source code.
 * This can be used for debugging, error reporting, and for providing detailed information
 * about the lexemes during the lexical analysis phase.
 */
typedef struct {
    /**
     * @brief The name of the source file.
     * 
     * This field stores the name of the file that is being analyzed. It helps to identify
     * where the token is located in case of errors or debugging.
     */
    const char *file_name;

    /**
     * @brief The row (line) number of the token.
     * 
     * This field stores the line number where the token was found in the source code.
     * Line numbering starts from 1.
     */
    size_t row;

    /**
     * @brief The column number of the token.
     * 
     * This field stores the column number (character position) within the line where the
     * token was found. Column numbering starts from 1.
     */
    size_t column;
} position_t;
