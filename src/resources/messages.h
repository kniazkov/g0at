/**
 * @file messages.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definitions for localized error messages.
 */

#pragma once

#include "goat/unicode.h"

/** @brief Structure to hold localized error messages. */
typedef struct {
    const gchar_t *native_targets_available;
    const gchar_t *native_targets_none;
    const gchar_t *native_targets_failed;
    const gchar_t *native_targets_selected;
    const gchar_t const *help;
    const gchar_t const *memory_leak;
    const gchar_t const *no_input_file;
    const gchar_t const *unknown_option;
    const gchar_t const *missing_specification;
    const gchar_t const *interactive_conflict;
    const gchar_t const *interactive_prompt;
    const gchar_t const *continuation_prompt;
    const gchar_t const *bad_graph_file;
    const gchar_t const *bad_optimization_level;
    const gchar_t const *bad_c_options;
    const gchar_t const *bad_native_mode;
    const gchar_t const *bad_native_probe_timeout;
    const gchar_t const *bad_native_options;
    const gchar_t const *native_report_conflict;
    const gchar_t const *cannot_write_native_report;
    const gchar_t const *native_prepare_failed;
    const gchar_t const *cannot_write_c_file;
    const gchar_t const *c_omitted;
    const gchar_t const *native_compile_failed;
    const gchar_t const *native_diagnostics_truncated;
    const gchar_t const *native_input_conflict;
    const gchar_t const *no_graphviz;
    const gchar_t const *graphviz_failed;
    const gchar_t const *duplicate_parameter;
    const gchar_t const *cannot_read_source_file;
    const gchar_t const *cannot_write_analysis_file;
    const gchar_t const *compilation_warning;
    const gchar_t const *compilation_error;
    const gchar_t const *critical_compilation_error;
    const gchar_t const *unknown_symbol;
    const gchar_t const *access_denied_statically;
    const gchar_t const *integer_literal_out_of_range; /**< Integer literal exceeds int64_t. */
    const gchar_t const *unclosed_quotation_mark;
    const gchar_t const *invalid_escape_sequence;
    const gchar_t const *empty_character_literal;
    const gchar_t const *invalid_character_literal;
    const gchar_t const *unclosed_opening_bracket;
    const gchar_t const *missing_opening_bracket;
    const gchar_t const *brackets_do_not_match;
    const gchar_t const *not_a_statement;
    const gchar_t const *expected_expression;
    const gchar_t const *invalid_conditional;
    const gchar_t const *invalid_switch;
    const gchar_t const *duplicate_case;
    const gchar_t const *invalid_loop;
    const gchar_t const *loop_jump_outside;

    /** @brief Invalid prototype construction syntax. */
    const gchar_t const *expected_inheritance_constructor;
    const gchar_t const *expected_lvalue;
    const gchar_t const *expected_comma_between_args;
    const gchar_t const *expected_expr_after_comma;
    const gchar_t const *expected_var_declaration;
    const gchar_t const *invalid_access_modifier;
    const gchar_t const *expected_const_declaration;
    const gchar_t const *expected_var_after_comma;
    const gchar_t const *expected_const_after_comma;
    const gchar_t const *invalid_var_declaration_syntax;
    const gchar_t const *invalid_const_declaration_syntax;
    const gchar_t const *invalid_function_argument;
    const gchar_t const *invalid_parenthesized_expression;
    const gchar_t const *reserved_this_name;
    const gchar_t const *reserved_launch_constant;
    const gchar_t const *variable_used_before_declaration;
    const gchar_t const *implicit_constant_declaration_invalid;
    const gchar_t const *expected_condition_after_if;
    const gchar_t const *expected_statement_after_if;
    const gchar_t const *expected_statement_after_else;
    const gchar_t const *else_without_if;
    const gchar_t const *duplicate_else_branch;
    const gchar_t *expected_try_statement;
    const gchar_t *expected_catch;
    const gchar_t *invalid_catch_binding;
    const gchar_t *expected_catch_block;
    const gchar_t *expected_finally_statement;
    const gchar_t *finally_without_try;
    const gchar_t *catch_without_try;
    const gchar_t *expected_throw_value;
    const gchar_t *uncaught_exception;
    const gchar_t *invalid_for_header;
    const gchar_t *invalid_iteration_source;
    const gchar_t *expected_statement_after_for;
    // add other
} messages_t;

/** @brief Returns the current set of messages based on the selected language. */
const messages_t *get_messages();

/** @brief Sets the language for error messages. */
void set_language(const char *lang);

/** @brief Initializes the message structure based on the environment variable. */
void init_messages();
