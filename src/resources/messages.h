/**
 * @file messages.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definitions for localized error messages.
 */

#pragma once

#include <wchar.h>

/** @brief Structure to hold localized error messages. */
typedef struct {
    const wchar_t const *help;
    const wchar_t const *memory_leak;
    const wchar_t const *no_input_file;
    const wchar_t const *unknown_option;
    const wchar_t const *missing_specification;
    const wchar_t const *bad_graph_file;
    const wchar_t const *bad_optimization_level;
    const wchar_t *bad_c_options;
    const wchar_t *cannot_write_c_file;
    const wchar_t *c_omitted;
    const wchar_t const *no_graphviz;
    const wchar_t const *graphviz_failed;
    const wchar_t const *duplicate_parameter;
    const wchar_t const *cannot_read_source_file;
    const wchar_t const *cannot_write_analysis_file;
    const wchar_t const *compilation_warning;
    const wchar_t const *compilation_error;
    const wchar_t const *critical_compilation_error;
    const wchar_t const *unknown_symbol;
    const wchar_t const *unclosed_quotation_mark;
    const wchar_t const *invalid_escape_sequence;
    const wchar_t const *unclosed_opening_bracket;
    const wchar_t const *missing_opening_bracket;
    const wchar_t const *brackets_do_not_match;
    const wchar_t const *not_a_statement;
    const wchar_t const *expected_expression;
    const wchar_t const *expected_lvalue;
    const wchar_t const *expected_comma_between_args;
    const wchar_t const *expected_expr_after_comma;
    const wchar_t const *expected_var_declaration;
    const wchar_t const *expected_const_declaration;
    const wchar_t const *expected_var_after_comma;
    const wchar_t const *expected_const_after_comma;
    const wchar_t const *invalid_var_declaration_syntax;
    const wchar_t const *invalid_const_declaration_syntax;
    const wchar_t const *invalid_function_argument;
    const wchar_t const *invalid_parenthesized_expression;
    const wchar_t const *variable_used_before_declaration;
    const wchar_t const *expected_condition_after_if;
    const wchar_t const *expected_statement_after_if;
    const wchar_t const *expected_statement_after_else;
    const wchar_t const *else_without_if;
    const wchar_t const *duplicate_else_branch;
    const wchar_t *expected_try_statement;
    const wchar_t *expected_catch;
    const wchar_t *invalid_catch_binding;
    const wchar_t *expected_catch_block;
    const wchar_t *catch_without_try;
    const wchar_t *expected_throw_value;
    const wchar_t *uncaught_exception;
    // add other
} messages_t;

/** @brief Returns the current set of messages based on the selected language. */
const messages_t *get_messages();

/** @brief Sets the language for error messages. */
void set_language(const char *lang);

/** @brief Initializes the message structure based on the environment variable. */
void init_messages();
