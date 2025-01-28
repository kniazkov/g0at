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
 * @brief Forward declaration of scanner structure.
 */
typedef struct scanner_t scanner_t;

/**
 * Forward declaration for parser memory structure.
 */
typedef struct parser_memory_t parser_memory_t;

/**
 * @struct scanner_t
 * @brief Structure to represent the scanner for lexical analysis.
 *
 * This structure holds the necessary state for performing lexical analysis, including
 * the current character in the source code, the current position in the file, a memory
 * manager for tokens and syntax tree nodes, and a pointer to token groups organized by type or role.
 */
struct scanner_t {
    /**
     * @brief The current position in the source code.
     * 
     * This structure holds the current position of the scanner in the source code,
     * including the file name, offset, row (line), and column number.
     */
    full_position_t position;

    /**
     * @brief Pointer to the memory manager used for token and syntax tree node allocation.
     * 
     * This memory manager consolidates memory arenas for tokens and syntax tree nodes,
     * simplifying function signatures and improving code maintainability.
     */
    parser_memory_t *memory;

    /**
     * @brief Pointer to token groups organized by type or role.
     * 
     * This pointer refers to token groups that are partially populated by the scanner.
     * The groups are typically created outside the scanner and passed to it for filling
     * during lexical analysis.
     */
    token_groups_t *groups;
};

/**
 * @brief Creates a new scanner for lexical analysis.
 * @param file_name The name of the file being scanned.
 * @param code The source code to be analyzed, represented as a wide-character string.
 * @param memory A pointer to the `parser_memory_t` structure, which manages memory
 *  allocation for tokens and syntax tree nodes.
 * @param groups A pointer to the `token_groups_t` structure, which organizes tokens
 *  by type or role. The scanner populates these groups during lexical analysis.
 * @return A pointer to the newly created `scanner_t` structure.
 */
scanner_t *create_scanner(const char *file_name, wchar_t *code, parser_memory_t *memory,
        token_groups_t *groups);


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
