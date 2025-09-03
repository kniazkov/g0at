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

#pragma once

#include <stdbool.h>

#include "lib/value.h"
#include "scanner/position.h"

/**
 * @brief Forward declaration of compilation error structure.
 */
typedef struct compilation_error_t compilation_error_t;

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
 * When multiple errors occur, they can be linked together in a chain using the `next` field,
 * forming a linked list of errors.
 */
struct compilation_error_t {
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
    string_view_t message;

    /**
     * @brief Indicates whether the error is critical.
     * 
     * If this field is set to `true`, the error is considered critical, and parsing should
     * stop immediately. Non-critical errors allow the parser to continue processing tokens,
     * potentially detecting additional errors in the same pass.
     */
    bool critical;

    /**
     * @brief Pointer to the next error in the chain.
     * 
     * This field links to the next `compilation_error_t` in the list of errors.
     * If there are no additional errors, this field is set to `NULL`.
     */
    compilation_error_t *next;
};

/**
 * @brief Creates a compilation error from a token with optional formatted message.
 * 
 * This function creates a `compilation_error_t` based on a given token.
 * It copies the token's position (begin and end), and depending on the presence of a
 * format string, it either uses the token's text or generates a new message
 * using the specified format and arguments.
 * 
 * @param arena The memory arena for allocating the error descriptor.
 * @param token The token from which to create the error descriptor.
 * @param format A format string to generate a custom error message (optional).
 * @param ... The arguments to be used with the format string (if provided).
 * 
 * @return A pointer to the created `compilation_error_t` structure, containing the error details.
 */
compilation_error_t *create_error_from_token(arena_t *arena, const token_t *token,
    const wchar_t *format, ...);

/**
 * @brief Reverses a linked list of compilation errors in-place.
 * 
 * This function takes a pointer to the head of a singly linked list of `compilation_error_t`
 * structures and reverses the list. After reversal, the original last error becomes the new head.
 * 
 * @param head Pointer to the first error in the original list.
 * @return Pointer to the new head of the reversed list.
 */
compilation_error_t *reverse_compilation_errors(compilation_error_t *head);