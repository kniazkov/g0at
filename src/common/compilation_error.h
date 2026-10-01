/**
 * @file compilation_error.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the structure and functions for handling compilation diagnostics.
 */

#pragma once

#include "position.h"
#include "lib/value.h"

typedef struct compilation_error_t compilation_error_t;

typedef struct token_t token_t;

typedef struct node_t node_t;

typedef struct arena_t arena_t;

/** @brief Enumeration of diagnostic severity levels. */
typedef enum {
    WARNING,  /**< Non-fatal diagnostic that reports a suspicious construct. */
    ERROR,    /**< Regular compilation error that indicates invalid source code. */
    CRITICAL  /**< Fatal diagnostic that should stop further processing. */
} compilation_error_severity_t;

/**
 * @brief A compilation diagnostic.
 *
 * It is used for reporting issues encountered during the lexical, syntactical, or semantic analysis
 * stages.
 */
struct compilation_error_t {
    /** @brief The source range occupied by the diagnostic. */
    position_range_t *position;

    /**
     * @brief The diagnostic message.
     *
     * For example, it might contain messages like "Unexpected token", "Undefined variable", etc.
     */
    string_view_t message;

    /** @brief Severity level of the diagnostic. */
    compilation_error_severity_t severity;

    /**
     * @brief Pointer to the next diagnostic in the chain.
     *
     * If there are no additional diagnostics, this field is set to `NULL`.
     */
    compilation_error_t *next;
};

/** @brief Creates a compilation diagnostic from a token with a formatted message. */
compilation_error_t *create_error_from_token(arena_t *arena, const token_t *token,
        compilation_error_severity_t severity, const wchar_t *format, ...);

/** @brief Creates a compilation diagnostic from an AST node with a formatted message. */
compilation_error_t *create_error_from_node(arena_t *arena, const node_t *node,
        compilation_error_severity_t severity, const wchar_t *format, ...);

/**
 * @brief Reverses a linked list of compilation diagnostics in-place.
 *
 * After reversal, the original last diagnostic becomes the new head.
 */
compilation_error_t *reverse_compilation_errors(compilation_error_t *head);

/** @brief Determines the most severe diagnostic in a linked list. */
compilation_error_severity_t get_most_severe_compilation_error(
        const compilation_error_t *head);