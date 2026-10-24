/** @file source_includes.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared syntax-only discovery of literal source inclusions and imports.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

/** @brief One source occurrence; offset counts decoded Unicode scalars. */
typedef struct {
    /** @brief Decoded literal: included path or imported module specification. */
    char *path;
    size_t row, column, offset;
} source_include_t;

/** @brief Owned source-order list. Release with clear_source_includes. */
typedef struct {
    source_include_t *items;
    size_t count;
} source_includes_t;

/** @brief Static diagnostic text and one-based location (offset is zero-based). */
typedef struct {
    size_t row, column, offset;
    const char *message;
} source_include_error_t;

/** @brief Uses the compiler scanner; never evaluates code or opens an included file.
 * Input remains unchanged. Validates UTF-8, lexical tokens, brackets and literal
 * include operands, including inside functions and dead branches. General
 * statement/type validation remains the compiler's responsibility. On failure
 * no partial list escapes. filename is borrowed only during this call. Initialize
 * out to zero and clear before reuse. Empty input is valid.
 */
bool parse_source_includes(const char *filename,
                           const char *utf8,
                           size_t length,
                           source_includes_t *out,
                           source_include_error_t *error);
/** @brief Discovers literal imports with the same lexical validation and ownership rules.
 * Scans all branches and function bodies without evaluating code. Comments,
 * string contents and member accesses (such as object.import) are ignored.
 * Each path contains the original decoded module specification; module names
 * and versions are interpreted by the caller. No import keyword is reserved.
 */
bool parse_source_imports(const char *filename,
                          const char *utf8,
                          size_t length,
                          source_includes_t *out,
                          source_include_error_t *error);
void clear_source_includes(source_includes_t *includes);
