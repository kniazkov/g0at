/**
 * @file source_builder.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines structures and functions for building source code in multiple languages.
 */

#pragma once

#include "lib/value.h"

#include <stddef.h>

typedef struct line_of_code_t line_of_code_t;

typedef struct source_builder_t source_builder_t;

/** @brief A single line of code in the source code generation process. */
struct line_of_code_t {
    /** @brief The indentation level for this line of code. */
    size_t indent;

    /**
     * @brief The text content of the line of code.
     *
     * It is structure that contains the string data and metadata, such as length and whether the
     * string needs to be freed after use.
     */
    string_value_t text;
};

/** @brief Building source code in various formats from the syntax tree. */
struct source_builder_t {
    /** @brief Array of lines of code. */
    line_of_code_t *lines;

    /** @brief The current number of lines in the builder. */
    size_t count;

    /** @brief The maximum capacity of the `lines` array. */
    size_t capacity;
};

/** @brief Creates a new source builder. */
source_builder_t *create_source_builder();

/** @brief Adds a line and takes ownership of text according to its should_free flag. */
void add_formatted_source(source_builder_t *builder, size_t indent, string_value_t text);

/** @brief Convenience macro for adding static string literals as source lines. */
#define add_static_source(builder, indent, text)                                                   \
    add_formatted_source(builder, indent, STATIC_STRING(text))

/** @brief Appends to the last line, consuming text according to its should_free flag. */
void append_formatted_source(source_builder_t *builder, string_value_t text);

/** @brief Convenience macro for appending static string literals to source lines. */
#define append_static_source(builder, text) append_formatted_source(builder, STATIC_STRING(text))

/** @brief Adds a line of source code with a specified indentation. */
void add_source(source_builder_t *builder, size_t indent, const wchar_t *format, ...);

/** @brief Appends text to the last line of source code. */
void append_source(source_builder_t *builder, const wchar_t *format, ...);

/** @brief Returns the assembled source; release with FREE_STRING(). The builder remains separate.
 */
string_value_t build_source(source_builder_t *builder);

/** @brief Destroys a source builder. */
void destroy_source_builder(source_builder_t *builder);
