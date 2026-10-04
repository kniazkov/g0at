/** @file c_control.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Exact numeric comparisons and structured C control flow.
 */
#pragma once
#include "c_generation.h"

/** @brief Implements node_vtbl_t::generate_c_code for numeric comparisons. */
c_generated_expression_t c_comparison(const node_t *node, c_generation_context_t *context);
/** @brief Implements node_vtbl_t::generate_c_code for boolean literals. */
c_generated_expression_t c_boolean(const node_t *node, c_generation_context_t *context);
/** @brief Implements node_vtbl_t::generate_indented_c_code for if/else. */
bool c_emit_if(const node_t *node,
               c_generation_context_t *context,
               source_builder_t *builder,
               size_t indent);
/** @brief Implements node_vtbl_t::generate_indented_c_code for a braced statement list. */
bool c_emit_block(const node_t *node,
                  c_generation_context_t *context,
                  source_builder_t *builder,
                  size_t indent);
/** @brief Implements node_vtbl_t::generate_indented_c_code for an expression statement. */
bool c_emit_statement(const node_t *node,
                      c_generation_context_t *context,
                      source_builder_t *builder,
                      size_t indent);
/** @brief Copies expression setup at its evaluation point. */
void c_emit_prelude(const source_builder_t *prelude, source_builder_t *builder, size_t indent);
/** @brief Emits the standalone exact integer/real comparison helper. */
void c_control_helpers(source_builder_t *builder);

/** @brief Emits a scoped loop with sequenced condition, body, and step. */
bool c_emit_for(const node_t *node,
                c_generation_context_t *context,
                source_builder_t *builder,
                size_t indent);
