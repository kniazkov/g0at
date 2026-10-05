/** @file unused_bindings.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Fixed-point elimination of unread lexical bindings after scalar simplification.
 */
#pragma once
#include "graph/node.h"

/** @brief Removes unread storage while retaining potentially observable evaluation. */
void eliminate_unused_bindings(node_t *root, arena_t *arena);
