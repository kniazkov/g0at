/** @file c_call.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Calls to exact statically proven numeric specializations.
 */
#pragma once
#include "c_generation.h"

/** @brief Implements node_vtbl_t::generate_c_code with Goat's right-to-left argument order. */
c_generated_expression_t c_call(const node_t *node, c_generation_context_t *context);
/** @brief Validates callee bindings and emits their numeric prototypes. */
bool c_emit_callee_prototypes(c_generation_context_t *context, source_builder_t *builder);
