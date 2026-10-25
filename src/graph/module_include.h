/** @file module_include.h
 * @copyright 2026 Ivan Kniazkov
 * @brief A source include resolved to a module-local file at compilation time.
 */
#pragma once
#include "node.h"
/** @brief Create an arena-owned expression; literal is retained for source display only. */
node_t *create_module_include_node(arena_t *arena, uint32_t file, node_t *literal);
