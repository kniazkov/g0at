/**
 * @file messages.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definitions for localized error messages.
 * 
 * This file defines the structure for holding localized error messages used during the parsing
 * of source code and the compilation to bytecode. It supports multiple languages by storing
 * different sets of messages.
 */

#pragma once

#include <wchar.h>

/**
 * @struct messages_t
 * @brief Structure to hold localized error messages.
 * 
 * This structure contains pointers to localized error messages, which are used during
 * the parsing and compilation process. Each message is a wide-character string. The messages
 * include various error messages related to symbol recognition, and more can be added as needed.
 */
typedef struct {
    const wchar_t const *memory_leak;
    const wchar_t const *no_input_file;
    const wchar_t const *unknown_option;
    const wchar_t const *cannot_read_source_file;
    const wchar_t const *compilation_error;
    const wchar_t const *unknown_symbol;
    const wchar_t const *unclosed_quotation_mark;
    const wchar_t const *invalid_escape_sequence;
    const wchar_t const *unclosed_opening_bracket;
    const wchar_t const *missing_opening_bracket;
    const wchar_t const *brackets_do_not_match;
    const wchar_t const *not_a_statement;
    const wchar_t const *expected_expression;
    // add other
} messages_t;

/**
 * @brief Returns the current set of messages based on the selected language.
 * 
 * This function returns a pointer to the `messages_t` structure, which contains 
 * the localized error messages used during the parsing of source code and 
 * compilation to bytecode, for the selected language.
 * 
 * @return A pointer to the current `messages_t` structure.
 */
const messages_t *get_messages();

/**
 * @brief Sets the language for error messages.
 * 
 * This function selects the language for error messages based on the provided language code.
 * The language code is compared case-insensitively.
 * 
 * @param lang The language code (for now, "en" for English, "ru" for Russian).
 */
void set_language(const char *lang);

/**
 * @brief Initializes the message structure based on the environment variable.
 * 
 * This function reads the `GOAT_LANGUAGE` environment variable and, if set, uses its value
 * to set the language for error and informational messages.
 * 
 * If the environment variable is not set, the default language will be English.
 * 
 * @note This function is called automatically at program startup to ensure the correct
 *  language is used based on the environment settings.
 */
void init_messages();
