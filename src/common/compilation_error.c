/**
 * @file compilation_error.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Provides helper functions for allocating and populating compilation diagnostics.
 *
 * This file contains functions for creating and managing `compilation_error_t` structures,
 * which represent diagnostics that occur during compilation. These diagnostics include
 * information such as the source position, a descriptive message, and a severity level.
 */

#include <assert.h>
#include <stdarg.h>

#include "compilation_error.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "scanner/token.h"
#include "graph/node.h"

/**
 * @brief Creates a compilation diagnostic from a source range and a variable argument list.
 *
 * This helper allocates and initializes a `compilation_error_t` structure, assigns the given
 * source range to it, formats the diagnostic message, and sets the severity to @ref CRITICAL.
 *
 * @param arena The memory arena for allocating the diagnostic descriptor.
 * @param position The source range associated with the diagnostic.
 * @param severity The severity of the diagnostic message.
 * @param format A format string used to generate the diagnostic message.
 * @param args Variable argument list for the format string.
 * @return A pointer to the created diagnostic descriptor.
 */
static compilation_error_t *create_error_from_position_vargs(arena_t *arena,
        position_range_t *position, compilation_error_severity_t severity, 
        const wchar_t *format, va_list args) {
    assert(format != NULL);

    compilation_error_t *error = (compilation_error_t *)alloc_from_arena(
        arena, sizeof(compilation_error_t));
    error->position = position;
    string_value_t value = format_string_vargs(format, args);
    if (value.data != NULL) {
        error->message = copy_string_to_arena(arena, value.data, value.length);
        FREE_STRING(value);
    } else {
        error->message = EMPTY_STRING_VIEW;
    }
    error->severity = severity;
    error->next = NULL;
    return error;
}

compilation_error_t *create_error_from_token(arena_t *arena, const token_t *token,
        compilation_error_severity_t severity, const wchar_t *format, ...) {
    compilation_error_t *error;
    va_list args;

    va_start(args, format);
    error = create_error_from_position_vargs(arena, token->position, severity, format, args);
    va_end(args);

    return error;
}

compilation_error_t *create_error_from_node(arena_t *arena, const node_t *node,
        compilation_error_severity_t severity, const wchar_t *format, ...) {
    compilation_error_t *error;
    va_list args;

    va_start(args, format);
    error = create_error_from_position_vargs(arena, node->position, severity, format, args);
    va_end(args);

    return error;
}

compilation_error_t *reverse_compilation_errors(compilation_error_t *head) {
    compilation_error_t *prev = NULL;
    compilation_error_t *curr = head;

    while (curr) {
        compilation_error_t *next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }

    return prev;
}

compilation_error_severity_t get_most_severe_compilation_error(
        const compilation_error_t *head) {
    compilation_error_severity_t result = WARNING;

    while (head) {
        if (head->severity > result) {
            result = head->severity;
            if (result == CRITICAL) {
                return result;
            }
        }
        head = head->next;
    }

    return result;
}
