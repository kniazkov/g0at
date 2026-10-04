/** @file c_expression.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Per-signature numeric expression proofs, separate from shared AST flags.
 */
#pragma once

#include "c_contract.h"

/** @brief One pointwise representation; UNKNOWN means no expression proof. */
typedef struct c_expression_proof_t {
    struct c_expression_proof_t *next;
    const node_t *node;
    c_value_type_t type;
    bool discardable;                  /**< Total scalar evaluation with no writes or calls. */
    const lattice_element_t *constant; /**< Exact across every generic visit, or NULL. */
} c_expression_proof_t;

/** @brief Shared by branch states during one isolated generic evaluation. */
typedef struct c_expression_context_t {
    arena_t *arena;
    c_expression_proof_t *head, *tail;
    struct function_call_graph_t *graph; /**< NULL for expression-only checking. */
    const function_summary_t *summary;
    struct c_binding_t **bindings; /**< Borrowed storage accumulator; shared across paths. */
    struct c_call_t *calls;
} c_expression_context_t;

/** @brief Finds a recorded proof without consulting shared AST flags. */
c_value_type_t c_expression_type(const c_expression_context_t *context, const node_t *node);

/** @brief Records a virtual proof; repeated visits intersect, never promote unknown facts. */
void record_c_expression(c_expression_context_t *context,
                         const node_t *node,
                         const lattice_element_t *value);

/** @brief Proves numeric operands for total arithmetic or exact numeric comparisons. */
bool c_numeric_operands(const node_t *node, const c_expression_context_t *context);

/** @brief Stable representation names for diagnostics. */
const wchar_t *c_expression_type_name(c_value_type_t type);

/** @brief Checks expressions with generic parameters; calls/control still need a body proof. */
void analyze_function_c_expressions(node_t *root);

/** @brief Returns a discardable constant proved for every visit of this signature. */
const lattice_element_t *c_expression_constant(const c_expression_context_t *context,
                                               const node_t *node);

/** @brief Exact scalar equality, preserving signed zero; NaNs are not reused. */
bool c_constants_equal(const lattice_element_t *left, const lattice_element_t *right);
