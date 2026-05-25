/**
 * @file compilation_error.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the structure and functions for handling compilation diagnostics.
 *
 * This file contains the definition of the `compilation_error_t` structure, which is used
 * to represent diagnostics encountered during the lexical, syntactical, or semantic
 * analysis phases. It also includes utility functions for creating, handling, and reporting
 * such diagnostics in the compiler or interpreter.
 */

#pragma once

#include "position.h"
#include "lib/value.h"

/**
 * @brief Forward declaration of compilation error structure.
 */
typedef struct compilation_error_t compilation_error_t;

/**
 * @brief Forward declaration of token structure.
 */
typedef struct token_t token_t;

/**
 * @brief Forward declaration of AST node structure.
 */
typedef struct node_t node_t;

/**
 * @struct arena_t
 * @brief Forward declaration for memory arena structure.
 */
typedef struct arena_t arena_t;

/**
 * @enum compilation_error_severity_t
 * @brief Enumeration of diagnostic severity levels.
 *
 * This enumeration describes how important a diagnostic is. The values are ordered
 * from least severe to most severe, which makes it possible to compare them directly
 * when scanning a list of diagnostics.
 */
typedef enum {
    WARNING,  /**< Non-fatal diagnostic that reports a suspicious construct. */
    ERROR,    /**< Regular compilation error that indicates invalid source code. */
    CRITICAL  /**< Fatal diagnostic that should stop further processing. */
} compilation_error_severity_t;

/**
 * @struct compilation_error_t
 * @brief Structure to represent a compilation diagnostic.
 *
 * This structure holds information about a specific diagnostic, including its
 * position in the source code, the diagnostic message, and the severity level.
 * It is used for reporting issues encountered during the lexical, syntactical,
 * or semantic analysis stages. When multiple diagnostics occur, they can be
 * linked together in a chain using the `next` field, forming a linked list.
 */
struct compilation_error_t {
    /**
     * @brief The source range occupied by the diagnostic.
     *
     * This field stores the beginning and ending positions of the problematic
     * entity in the source code.
     */
    position_range_t *position;

    /**
     * @brief The diagnostic message.
     *
     * This field contains a string describing the issue. For example, it might contain
     * messages like "Unexpected token", "Undefined variable", etc.
     */
    string_view_t message;

    /**
     * @brief Severity level of the diagnostic.
     *
     * This field indicates how serious the diagnostic is.
     */
    compilation_error_severity_t severity;

    /**
     * @brief Pointer to the next diagnostic in the chain.
     *
     * This field links to the next `compilation_error_t` in the list of diagnostics.
     * If there are no additional diagnostics, this field is set to `NULL`.
     */
    compilation_error_t *next;
};

/**
 * @brief Creates a compilation diagnostic from a token with a formatted message.
 *
 * This function creates a `compilation_error_t` based on a given token.
 * It reuses the token's source range and generates a diagnostic message using
 * the specified format string and arguments.
 *
 * @param arena The memory arena for allocating the diagnostic descriptor.
 * @param token The token from which to create the diagnostic descriptor.
 * @param severity The severity of the diagnostic message.
 * @param format A format string used to generate the diagnostic message.
 * @param ... The arguments to be used with the format string.
 * @return A pointer to the created `compilation_error_t` structure.
 */
compilation_error_t *create_error_from_token(arena_t *arena, const token_t *token,
        compilation_error_severity_t severity, const wchar_t *format, ...);

/**
 * @brief Creates a compilation diagnostic from an AST node with a formatted message.
 *
 * This function creates a `compilation_error_t` based on a given syntax tree node.
 * It reuses the node's source range and generates a diagnostic message using
 * the specified format string and arguments.
 *
 * @param arena The memory arena for allocating the diagnostic descriptor.
 * @param node The node from which to create the diagnostic descriptor.
 * @param severity The severity of the diagnostic message.
 * @param format A format string used to generate the diagnostic message.
 * @param ... The arguments to be used with the format string.
 * @return A pointer to the created `compilation_error_t` structure.
 */
compilation_error_t *create_error_from_node(arena_t *arena, const node_t *node,
        compilation_error_severity_t severity, const wchar_t *format, ...);

/**
 * @brief Reverses a linked list of compilation diagnostics in-place.
 *
 * This function takes a pointer to the head of a singly linked list of
 * `compilation_error_t` structures and reverses the list. After reversal,
 * the original last diagnostic becomes the new head.
 *
 * @param head Pointer to the first diagnostic in the original list.
 * @return Pointer to the new head of the reversed list.
 */
compilation_error_t *reverse_compilation_errors(compilation_error_t *head);

/**
 * @brief Determines the most severe diagnostic in a linked list.
 *
 * This function scans the entire linked list of diagnostics and returns the
 * highest severity level found in it. If the list contains at least one
 * @ref CRITICAL diagnostic, the function returns @ref CRITICAL immediately.
 *
 * If the list is empty, the function returns @ref WARNING.
 *
 * @param head Pointer to the first diagnostic in the list.
 * @return The highest severity level found in the list.
 */
compilation_error_severity_t get_most_severe_compilation_error(
        const compilation_error_t *head);