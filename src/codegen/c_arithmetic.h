/** @file c_arithmetic.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Sequenced numeric arithmetic for C emission.
 */
#pragma once

#include "c_generation.h"
#include "goat/unicode.h"

/** @brief Lowers +, - or * with explicit operand evaluation and numeric rounding. */
c_generated_expression_t
c_binary_arithmetic(const node_t *node, c_generation_context_t *context, goat_char_t operation);
/** @brief Lowers unary signs with saturating integer negation. */
c_generated_expression_t
c_unary_arithmetic(const node_t *node, c_generation_context_t *context, bool negative);
/** @brief Implements node_vtbl_t::generate_c_code for parentheses. */
c_generated_expression_t c_parenthesized(const node_t *node, c_generation_context_t *context);

/** @brief Appends an operand's prelude and evaluates it once into a typed temporary. */
string_value_t c_capture_operand(source_builder_t *prelude,
                                 c_generation_context_t *context,
                                 const c_generated_expression_t *operand,
                                 c_value_type_t type);
