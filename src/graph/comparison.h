/** @file comparison.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared implementation of comparison expression nodes.
 */
#pragma once
#include "binary_operation.h"
#include "lib/comparison.h"

comparison_kind_t node_comparison_kind(node_type_t type);
expression_t *create_comparison_node(arena_t *arena,
                                     comparison_kind_t kind,
                                     expression_t *left,
                                     expression_t *right);
