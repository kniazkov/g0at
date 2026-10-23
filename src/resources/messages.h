/**
 * @file messages.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definitions for localized error messages.
 */

#pragma once

#include "goat/unicode.h"

/** @brief Structure to hold localized error messages. */
typedef struct {
    const goat_char_t const *help;
    const goat_char_t const *memory_leak;
    const goat_char_t const *no_input_file;
    const goat_char_t const *unknown_option;
    const goat_char_t const *missing_specification;
    const goat_char_t const *interactive_conflict;
    const goat_char_t const *interactive_prompt;
    const goat_char_t const *continuation_prompt;
    const goat_char_t const *bad_graph_file;
    const goat_char_t const *bad_optimization_level;
    const goat_char_t const *bad_c_options;
    const goat_char_t const *bad_native_mode;
    const goat_char_t const *bad_native_options;
    const goat_char_t const *native_report_conflict;
    const goat_char_t const *cannot_write_native_report;
    const goat_char_t const *native_prepare_failed;
    const goat_char_t const *cannot_write_c_file;
    const goat_char_t const *c_omitted;
    const goat_char_t const *native_compile_failed;
    const goat_char_t const *native_diagnostics_truncated;
    const goat_char_t const *native_input_conflict;
    const goat_char_t const *no_graphviz;
    const goat_char_t const *graphviz_failed;
    const goat_char_t const *duplicate_parameter;
    const goat_char_t const *cannot_read_source_file;
    const goat_char_t const *cannot_write_analysis_file;
    const goat_char_t const *compilation_warning;
    const goat_char_t const *compilation_error;
    const goat_char_t const *critical_compilation_error;
    const goat_char_t const *unknown_symbol;
    const goat_char_t const *access_denied_statically;
    const goat_char_t const *integer_literal_out_of_range; /**< Integer literal exceeds int64_t. */
    const goat_char_t const *unclosed_quotation_mark;
    const goat_char_t const *invalid_escape_sequence;
    const goat_char_t const *empty_character_literal;
    const goat_char_t const *invalid_character_literal;
    const goat_char_t const *unclosed_opening_bracket;
    const goat_char_t const *missing_opening_bracket;
    const goat_char_t const *brackets_do_not_match;
    const goat_char_t const *not_a_statement;
    const goat_char_t const *expected_expression;
    const goat_char_t const *invalid_conditional;
    const goat_char_t const *invalid_switch;
    const goat_char_t const *duplicate_case;
    const goat_char_t const *invalid_loop;
    const goat_char_t const *loop_jump_outside;

    /** @brief Invalid prototype construction syntax. */
    const goat_char_t const *expected_inheritance_constructor;
    const goat_char_t const *expected_lvalue;
    const goat_char_t const *expected_comma_between_args;
    const goat_char_t const *expected_expr_after_comma;
    const goat_char_t const *expected_var_declaration;
    const goat_char_t const *invalid_access_modifier;
    const goat_char_t const *expected_const_declaration;
    const goat_char_t const *expected_var_after_comma;
    const goat_char_t const *expected_const_after_comma;
    const goat_char_t const *invalid_var_declaration_syntax;
    const goat_char_t const *invalid_const_declaration_syntax;
    const goat_char_t const *invalid_function_argument;
    const goat_char_t const *invalid_parenthesized_expression;
    const goat_char_t const *reserved_this_name;
    const goat_char_t const *variable_used_before_declaration;
    const goat_char_t const *implicit_constant_declaration_invalid;
    const goat_char_t const *expected_condition_after_if;
    const goat_char_t const *expected_statement_after_if;
    const goat_char_t const *expected_statement_after_else;
    const goat_char_t const *else_without_if;
    const goat_char_t const *duplicate_else_branch;
    const goat_char_t *expected_try_statement;
    const goat_char_t *expected_catch;
    const goat_char_t *invalid_catch_binding;
    const goat_char_t *expected_catch_block;
    const goat_char_t *expected_finally_statement;
    const goat_char_t *finally_without_try;
    const goat_char_t *catch_without_try;
    const goat_char_t *expected_throw_value;
    const goat_char_t *uncaught_exception;
    const goat_char_t *invalid_for_header;
    const goat_char_t *invalid_iteration_source;
    const goat_char_t *expected_statement_after_for;
    // add other
} messages_t;

/** @brief Returns the current set of messages based on the selected language. */
const messages_t *get_messages();

/** @brief Sets the language for error messages. */
void set_language(const char *lang);

/** @brief Initializes the message structure based on the environment variable. */
void init_messages();
