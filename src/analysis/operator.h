/** @file operator.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared proofs for possible user-operator calls.
 */
#pragma once
#include "c_expression.h"

/** @brief Whether a node applies an overloadable operator to its first child. */
bool node_has_operator_receiver(const node_t *node);

/** @brief A missing proof is conservative; contexts never use shared immediate samples. */
bool node_may_dispatch_operator(const node_t *node, const c_expression_context_t *context);
