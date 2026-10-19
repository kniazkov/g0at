/** @file loop.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Structured loops and their nearest-loop transfers.
 */
#pragma once
#include "statement.h"
node_t *
create_while_node(arena_t *arena, expression_t *condition, statement_t *body, bool post_test);
/** @brief Creates break (next=false) or continue (next=true). */
node_t *create_loop_jump_node(arena_t *arena, bool next);
/** @brief Recognizes supported structured loop nodes. */
bool is_loop_node(const node_t *node);
/** @brief Finds transfers in the body, excluding nested loops and functions. */
bool loop_has_jump(const node_t *node, bool next);
/** @brief Emits a transfer and appends it to its enclosing loop's patch chain. */
instr_index_t generate_loop_jump(node_t *node, code_builder_t *code);
