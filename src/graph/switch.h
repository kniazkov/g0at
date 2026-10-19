/** @file switch.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Ordered switch branches; labels are evaluated in the enclosing scope.
 */
#pragma once
#include "statement.h"

typedef struct {
    size_t count; /**< Zero labels denotes the final default branch. */
    node_t **labels;
    node_t *body; /**< Scoped statement expression. */
} switch_branch_t;

node_t *
create_switch_node(arena_t *arena, node_t *selector, switch_branch_t *branches, size_t count);
/** @brief Returns the number of branches, including an optional default. */
size_t switch_branch_count(const node_t *node);
/** @brief Borrows the indexed branch, or returns NULL outside the range. */
const switch_branch_t *switch_branch(const node_t *node, size_t index);
