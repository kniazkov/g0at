/** @file replacement.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Visible optimization history with one executable child.
 */
#pragma once
#include "expression.h"
#include "statement.h"

/** @brief Creates an expression replacement; attachment is left to the caller. */
expression_t *
create_expression_replacement(arena_t *arena, expression_t *original, expression_t *replacement);
/** @brief Creates a statement replacement; attachment is left to the caller. */
statement_t *
create_statement_replacement(arena_t *arena, statement_t *original, statement_t *replacement);

/** @brief Checks for either synthetic replacement type. */
static inline bool is_replacement(const node_t *node) {
    return node->vtbl->type == NODE_EXPRESSION_REPLACEMENT
           || node->vtbl->type == NODE_STATEMENT_REPLACEMENT;
}

/** @brief Follows executable children, ignoring archived originals. */
const node_t *replacement_result(const node_t *node);

/** @brief Follows archived originals for identity and proof lookup; leaves the graph intact.
 */
const node_t *replacement_original(const node_t *node);
