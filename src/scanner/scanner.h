/**
 * @file scanner.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Provides the definition of the scanner structure and function prototypes
 *  for lexical analysis.
 *
 * This file defines the `scanner_t` structure, which represents the scanner for performing lexical
 * analysis, and provides function prototypes for manipulating and using the scanner.
 * The scanner reads the source code character by character, tracks its current position,
 * and uses an arena for memory allocation.
 */

#pragma once

#include <wchar.h>
#include "token.h"
#include "position.h"

/**
 * @brief Forward declaration of arena memory allocator to be used in the scanner.
 */
typedef struct arena_t arena_t;

/**
 * @struct scanner_t
 * @brief Structure to represent the scanner for lexical analysis.
 *
 * This structure holds the necessary state for performing lexical analysis, including
 * the current character in the source code, the current position in the file, and
 * the memory arena used for efficient memory allocation.
 */
typedef struct {
    /**
     * @brief Pointer to the current character in the source code.
     * 
     * This pointer points to the current character being analyzed in the source code.
     * It is updated as the scanner moves through the source code character by character.
     */
    wchar_t *ptr;

    /**
     * @brief The current position in the source code.
     * 
     * This structure holds the current position of the scanner in the source code,
     * including the file name, row (line), and column number.
     */
    position_t position;

    /**
     * @brief Pointer to the memory arena used for memory allocation.
     * 
     * The scanner uses this arena to allocate memory for tokens and other intermediate
     * data structures. The memory is freed in bulk when the arena is destroyed.
     */
    arena_t *arena;
} scanner_t;

/**
 * @brief Creates a new scanner for lexical analysis.
 * @param file_name The name of the file being scanned.
 * @param code The source code to be analyzed. It is a wide-character string.
 * @param arena The memory arena used for allocating memory for the scanner.
 * @return A pointer to the newly created `scanner_t` structure.
 */
scanner_t *create_scanner(const char *file_name, wchar_t *code, arena_t *arena);

/**
 * @brief Extracts the next token from the source code.
 * 
 * This function reads the source code character by character, trying to match
 * the current sequence of characters to a known token type (e.g., identifier, number, operator).
 * If a valid token is found, it is returned. If an invalid character or an unrecognized sequence
 * is encountered, a token with type `TOKEN_ERROR` is returned. When there are no more tokens,
 * the function returns NULL.
 * 
 * The function updates the scanner’s position as it processes the source code.
 * 
 * @param scan The scanner instance that tracks the current position and character.
 * @return A pointer to a `token_t` representing the next token, or `NULL` if no tokens are left.
 */
token_t *get_token(scanner_t *scan);
