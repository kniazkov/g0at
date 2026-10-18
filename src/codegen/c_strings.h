/** @file c_strings.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Sequenced native string conversion and literals.
 */
#pragma once
#include "c_generation.h"

/** @brief Emits str for a captured primitive; strings pass through unchanged. */
string_value_t c_string_from_value(source_builder_t *prelude,
                                   c_generation_context_t *context,
                                   c_value_type_t type,
                                   const wchar_t *value);
/** @brief Lowers a proven str call, preserving evaluation of extra arguments. */
c_generated_expression_t c_string_call(const node_t *node, c_generation_context_t *context);
/** @brief Emits static literal storage with explicit wchar_t code units. */
c_generated_expression_t
c_string_literal(const node_t *node, c_generation_context_t *context, string_view_t value);
