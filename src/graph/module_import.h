/** @file module_import.h
 * @copyright 2026 Ivan Kniazkov
 * @brief A module import bound to a canonical exact name/version descriptor.
 */
#pragma once
#include "node.h"
/** Copy identity into the arena; retain literal for source display only. */
node_t *create_module_import_node(arena_t *arena, string_view_t identity, node_t *literal);
