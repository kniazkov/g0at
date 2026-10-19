/** @file c_switch.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Native switch eligibility and sequenced equality dispatch.
 */
#pragma once
#include "analysis/c_expression.h"
#include "c_generation.h"
bool c_switch_supported(const node_t *, const lattice_element_t *, const c_expression_context_t *);
bool c_switch_falls_through(const node_t *, const c_expression_context_t *);
bool c_emit_switch(const node_t *, c_generation_context_t *, source_builder_t *, size_t);
