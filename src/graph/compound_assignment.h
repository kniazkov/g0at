/** @file compound_assignment.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Compound assignment as a retained target and an ordinary binary operation.
 */
#pragma once

#include "assignment.h"
#include "goat/unicode.h"
expression_t *create_compound_assignment_node(arena_t *arena,
                                              assignable_expression_t *left,
                                              expression_t *right,
                                              const gchar_t *symbol);
/** @brief Returns the target nested inside the ordinary operation. */
node_t *compound_assignment_target(const node_t *node);
/** @brief Whether a variable occurrence is the read/write target of a compound assignment. */
bool is_compound_assignment_target(const node_t *node);
