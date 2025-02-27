/**
 * @file source_builder.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the source builder for code generation.
 * 
 * This file contains the implementation of the `source_builder_t` structure and its related
 * functions. The source builder is responsible for generating source code in various languages,
 * such as Goat, C, and DOT. It allows for the construction of source code line by line, managing
 * indentation, and dynamically resizing the list of lines as more lines are added.
 */

#include <memory.h>

#include "source_builder.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

/**
 * @brief Define initial capacity for the source builder's lines array
 */
#define INITIAL_CAPACITY 64

source_builder_t *create_source_builder() {
    source_builder_t *builder = (source_builder_t *)ALLOC(sizeof(source_builder_t));
    builder->count = 0;
    builder->capacity = INITIAL_CAPACITY;
    builder->lines = (line_of_code_t *)ALLOC(builder->capacity * sizeof(line_of_code_t));
    return builder;
}

void add_formatted_line_of_source_code(source_builder_t *builder, size_t indent,
        string_value_t line) {
    if (builder->count == builder->capacity) {
        builder->capacity *= 2;
        line_of_code_t *new_lines = (line_of_code_t *)ALLOC(
            builder->capacity * sizeof(line_of_code_t));
        memcpy(new_lines, builder->lines, builder->count * sizeof(line_of_code_t));
        FREE(builder->lines);
        builder->lines = new_lines;
    }
    builder->lines[builder->count].indent = indent;
    builder->lines[builder->count].text = line;
    builder->count++;
}

void add_line_of_source_code(source_builder_t *builder, size_t indent, const wchar_t *format, ...) {
    va_list args;
    va_start(args, format);
    string_value_t formatted_line = format_string_vargs(format, args);
    add_formatted_line_of_source_code(builder, indent, formatted_line);
    va_end(args);
}

/**
 * @brief Define tabulation width (one "unit" of indentation)
 */
#define TAB_SIZE 4

/**
 * @brief Tabulation as a set of spaces
 */
static wchar_t *tabulation = L"    ";

string_value_t build_source_code(source_builder_t *builder) {
    string_builder_t code_builder;
    string_value_t result = { NULL, 0, false };
    init_string_builder(&code_builder, 0);
    for (size_t index = 0; index < builder->count; index++) {
        line_of_code_t line = builder->lines[index];
        for (size_t indent = 0; indent < line.indent; indent++) {
            append_substring(&code_builder, tabulation, TAB_SIZE);
        }
        append_substring(&code_builder, line.text.data, line.text.length);
        result = append_char(&code_builder, L'\n');
    }
    return result;
}

void destroy_source_builder(source_builder_t *builder) {
    for (size_t index = 0; index < builder->count; index++) {
        if (builder->lines[index].text.should_free) {
            FREE(builder->lines[index].text.data);            
        }
    }
    FREE(builder->lines);
    FREE(builder);
}
