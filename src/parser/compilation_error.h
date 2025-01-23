/**
 * @file compilation_error.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines the structure and functions for handling compilation errors.
 *
 * This file contains the definition of the `compilation_error_t` structure, which is used
 * to represent compilation errors encountered during the lexical, syntactical, or semantic
 * analysis phases. It also includes utility functions for creating, handling, and reporting
 * compilation errors in the compiler or interpreter.
 */

#include "scanner/position.h"

/**
 * @brief Forward declaration of token structure.
 */
typedef struct token_t token_t;

/**
 * @struct arena_t
 * @brief Forward declaration for memory arena structure.
 */
typedef struct arena_t arena_t;

/**
 * @struct compilation_error_t
 * @brief Structure to represent a compilation error.
 * 
 * This structure holds information about a specific compilation error, including its
 * position in the source code, the error message, and the length of the message. It is used for
 * reporting issues encountered during the lexical, syntactical, or semantic analysis stages.
 */
typedef struct {
    /**
     * @brief The full position of the error's beginning in the source code.
     * 
     * This field stores the information about the file, row, column, and offset
     * where the error began. This helps pinpoint the exact starting location of the error.
     */
    full_position_t begin;

    /**
     * @brief The short position of the error's end in the source code.
     * 
     * This field stores the position of the character immediately after the error
     * occurred, represented by the row, column, and offset where the error ends.
     * This can be useful for indicating a range of affected source code.
     */
    short_position_t end;
    /**
     * @brief The error message.
     * 
     * This field contains a string describing the error. For example, it might contain
     * messages like "Unexpected token", "Syntax error", etc.
     */
    const wchar_t const *message;

    /**
     * @brief The length of the error message.
     */
    size_t message_length;
} compilation_error_t;

/**
 * @brief Creates a compilation error from a token.
 * 
 * This function creates a `compilation_error_t` from a token, copying the token's position
 * and message (text).
 * 
 * @param arena The memory arena for allocating the error descriptor.
 * @param token The token from which to create the error descriptor.
 * @return A pointer to the created `compilation_error_t` structure.
 */
compilation_error_t *create_error_from_token(arena_t *arena, const token_t *token);
