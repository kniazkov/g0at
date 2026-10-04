/** @file c_lowering.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Minimal numeric C lowering shared by node virtual methods.
 */
#pragma once
#include "c_generation.h"

const wchar_t *c_type_name(c_value_type_t type);
c_generated_expression_t
c_integer_literal(const node_t *node, c_generation_context_t *context, int64_t value);
c_generated_expression_t
c_real_literal(const node_t *node, c_generation_context_t *context, double value);
/** @brief Implements node_vtbl_t::generate_indented_c_code for a complete function. */
bool c_emit_function(const node_t *node,
                     c_generation_context_t *context,
                     source_builder_t *builder,
                     size_t indent);
/** @brief Implements node_vtbl_t::generate_indented_c_code for a statement sequence. */
bool c_emit_body(const node_t *node,
                 c_generation_context_t *context,
                 source_builder_t *builder,
                 size_t indent);
/** @brief Implements node_vtbl_t::generate_indented_c_code for a numeric return. */
bool c_emit_return(const node_t *node,
                   c_generation_context_t *context,
                   source_builder_t *builder,
                   size_t indent);
