/** @file c_body.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Numeric body and static-call eligibility, without C emission.
 */
#pragma once

#include "abstract_state.h"
#include "c_expression.h"
#include "function_call_graph.h"

/** @brief Checks statements virtually and expressions against isolated proofs. */
bool c_body_node_supported(const node_t *node, const c_expression_context_t *context);

/** @brief Implements node_vtbl_t::can_generate_c_code for statement containers. */
bool c_body_children(const node_t *node,
                     const lattice_element_t *value,
                     const c_expression_context_t *context);

/** @brief Records or checks one fixed scalar representation for a local binding. */
bool c_local_binding(const c_expression_context_t *context,
                     const declarator_t *declaration,
                     c_value_type_t type);

/** @brief Returns a known pure static callee's generic result; failed lookup forgets state. */
const lattice_element_t *interpret_c_call(const node_t *site,
                                          const lattice_element_t *const *args,
                                          size_t count,
                                          abstract_state_t *state);

/** @brief Proves a recorded static call and every argument, including ignored extras. */
bool c_call_supported(const node_t *site, const c_expression_context_t *context);

/** @brief Refines pure call returns, then eliminates invalid recursive body candidates. */
void analyze_function_c_bodies(function_call_graph_t *graph, size_t max_iterations);
