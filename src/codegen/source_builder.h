/**
 * @file source_builder.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines structures and functions for building source code in multiple languages.
 * 
 * This file contains the declarations of structures and functions for converting an abstract
 * syntax tree (AST) and bytecode into different types of source code outputs. The system supports
 * multiple formats, including Goat source code, Graphviz DOT format for visualizing
 * the syntax tree, assembly listings for the bytecode, and C code for parts of the AST that can be
 * compiled into native code.
 */

#pragma once

#include <stddef.h>

#include "lib/value.h"

/**
 * @typedef source_builder_t
 * @brief Forward declaration for the structure describing one line of source code.
 */
typedef struct line_of_code_t line_of_code_t;

/**
 * @typedef source_builder_t
 * @brief Forward declaration for the source builder structure.
 */
typedef struct source_builder_t source_builder_t;

/**
 * @struct line_of_code_t
 * @brief Represents a single line of code in the source code generation process.
 * 
 * This structure holds the information for a single line of code, including its indentation level
 * and the actual text content of the line. It is used during the process of generating formatted
 * source code, where proper indentation is important for readability and structure.
 */
struct line_of_code_t {
    /**
     * @brief The indentation level for this line of code.
     * 
     * This field specifies the number of spaces (or tabs, depending on the formatting convention)
     * to be used for indenting the line. The `indent` level is important for maintaining
     * the correct structure and readability of the generated source code.
     */
    size_t indent;

    /**
     * @brief The text content of the line of code.
     * 
     * This field holds the actual text of the line of code. It is structure that contains
     * the string data and metadata, such as length and whether the string needs to be freed
     * after use.
     */
    string_value_t text;
};


/**
 * @struct source_builder_t
 * @brief Structure for building source code in various formats from the syntax tree.
 * 
 * This structure is used for constructing and managing a list of source code lines during the
 * code generation process. It helps in formatting the output source code line by line, managing
 * indentation, and dynamically resizing the list as new lines are added.
 * 
 * Various generators using this structure support these  formats:
 *  - Goat source code
 *  - Graphviz DOT format (for visualizing the graph)
 *  - Assembly listing of bytecode
 *  - C code for native execution
 */
struct source_builder_t {
    /**
     * @brief Array of lines of code.
     * 
     * This field holds a dynamically allocated array of `line_of_code_t` structures.
     * Each structure represents a single line of code, including the indentation and the text
     * content of the line. The array grows dynamically as more lines are added.
     */
    line_of_code_t *lines;

    /**
     * @brief The current number of lines in the builder.
     * 
     * This field tracks how many lines have been added to the source builder.
     * It is used for indexing and determining when to resize the `lines` array.
     */
    size_t count;

    /**
     * @brief The maximum capacity of the `lines` array.
     * 
     * This field stores the maximum number of lines the `lines` array can currently hold.
     * If `count` exceeds this value, the array will be resized to accommodate additional lines.
     */
    size_t capacity;
};

/**
 * @brief Creates a new source builder.
 * 
 * This function initializes a new `source_builder_t` structure, allocating memory for the 
 * initial set of lines and setting up the structure's fields. The `lines` array is initially 
 * allocated with a default capacity, which can be expanded as needed.
 * 
 * @return A pointer to the newly created `source_builder_t` structure.
 */
source_builder_t *create_source_builder();

/**
 * @brief Adds a pre-formatted line of source code with a specified indentation.
 * 
 * This function appends a given wide-character string as a new line in the source code builder.
 * The added line is stored with the specified indentation. Ownership of the `line` is transferred
 * to the `source_builder_t` structure.
 * 
 * @param builder Pointer to the `source_builder_t` structure where the line will be added.
 * @param indent Number of spaces to use for indentation.
 * @param text Pre-formatted wide-character string to be added.
 */
void add_formatted_line_of_source(source_builder_t *builder, size_t indent,
    string_value_t text);

/**
 * @brief Appends text to the last line of source code without adding a new line.
 * 
 * This function concatenates the given pre-formatted text to the text of the last line
 * in the source builder. If no lines exist yet, it creates a new line with zero indentation.
 * The text is appended exactly as provided, without any additional formatting.
 * 
 * @param builder Pointer to the `source_builder_t` structure to modify.
 * @param text Pre-formatted wide-character string to be appended.
 */
void append_formatted_line_of_source(source_builder_t *builder, string_value_t text);

/**
 * @brief Adds a line of source code with a specified indentation.
 * 
 * This function adds a new line to the source code being built. The line is formatted using the
 * provided `format` string and arguments, and the formatted line is indented according to the
 * specified `indent` level. The resulting line is stored in the `source_builder_t` structure.
 * 
 * @param builder A pointer to the `source_builder_t` structure where the line will be added.
 * @param indent The number of spaces to use for indenting the line.
 * @param format The wide-character format string containing placeholders for the arguments.
 * @param ... A variable number of arguments to be substituted into the format string.
 */
void add_line_of_source(source_builder_t *builder, size_t indent, const wchar_t *format, ...);

/**
 * @brief Appends text to the last line of source code.
 * 
 * This function formats the given arguments according to the format string and appends
 * the result to the text of the last line in the source builder. If no lines exist yet,
 * it creates a new line with zero indentation.
 * 
 * @param builder Pointer to the `source_builder_t` structure to modify.
 * @param format The wide-character format string containing placeholders for the arguments.
 * @param ... A variable number of arguments to be substituted into the format string.
 */
void append_line_of_source(source_builder_t *builder, const wchar_t *format, ...);

/**
 * @brief Builds the complete source code from the lines stored in the source builder.
 * 
 * This function constructs the entire source code by iterating over all the lines stored
 * in the `source_builder_t`. Each line is prefixed with the appropriate number of indentation
 * spaces, and the lines are concatenated together to form a complete string. A tabulation of 4 
 * spaces per indentation level is applied for each line.
 * 
 * The result is returned as a `string_value_t`, containing the concatenated string representing
 * the full source code.
 * 
 * @param builder A pointer to the `source_builder_t` structure containing the source code lines.
 * @return A `string_value_t` structure containing the final source code as a wide-character
 *  string.
 */
string_value_t build_source(source_builder_t *builder);

/**
 * @brief Destroys a source builder.
 * 
 * This function frees the memory used by a `source_builder_t` structure, including the memory
 * allocated for the `lines` array. It also frees the memory associated with the `text` field
 * of each line (if applicable), which may require freeing the string data.
 * 
 * @param builder A pointer to the `source_builder_t` structure to be destroyed.
 */
void destroy_source_builder(source_builder_t *builder);
