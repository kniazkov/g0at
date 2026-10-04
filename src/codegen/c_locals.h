/** @file c_locals.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Fixed local storage and assignment lowering.
 */
#pragma once
#include "c_generation.h"

/** @brief Reserves storage in one Goat scope, without evaluating initializers. */
bool c_prepare_locals(const node_t *scope,
                      c_generation_context_t *context,
                      source_builder_t *builder,
                      size_t indent);
/** @brief Releases scope-owned bindings, retaining the borrowed prefix. */
void c_release_locals(c_generation_context_t *context, const c_generation_binding_t *saved);
/** @brief Implements node_vtbl_t::generate_indented_c_code for a declaration group. */
bool c_emit_declarations(const node_t *node,
                         c_generation_context_t *context,
                         source_builder_t *builder,
                         size_t indent);
/** @brief Implements node_vtbl_t::generate_indented_c_code for one initializer. */
bool c_emit_declarator(const node_t *node,
                       c_generation_context_t *context,
                       source_builder_t *builder,
                       size_t indent);
/** @brief Implements node_vtbl_t::generate_c_code for assignment to local storage. */
c_generated_expression_t c_assignment(const node_t *node, c_generation_context_t *context);

/** @brief Lowers prefix/postfix updates of fixed numeric local storage. */
c_generated_expression_t c_update(const node_t *node, c_generation_context_t *context);
