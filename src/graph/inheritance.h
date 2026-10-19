/** @file inheritance.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Prototype-based object construction.
 */
#pragma once
#include "statement_list.h"

/** @brief Creates an inheritance expression with a constructor block. */
node_t *
create_inheritance_node(arena_t *arena, expression_t *prototype, statement_list_t *constructor);
