/** @file statement_list.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Block expression with its own lexical environment.
 */
#pragma once

#include "expression.h"

/** @brief An arena-owned block and its ordered statements. */
typedef struct statement_list_t {
    expression_t base;  /**< Base expression. */
    list_t *statements; /**< Arena-owned statement list, filled after parsing the body. */
} statement_list_t;

/** @brief Creates a block whose body will be filled separately. */
statement_list_t *create_statement_list_node(arena_t *arena);

/** @brief Sets the block's statement list. */
void fill_statement_list_node(statement_list_t *node, list_t *statements);
