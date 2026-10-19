/** @file index_access.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Assignable indexing expression with an arbitrary list of indices.
 */
#pragma once
#include "assignable_expression.h"

/** @brief Indexed access: receiver followed by zero or more index expressions. */
typedef struct {
    assignable_expression_t base; /**< Assignable expression base. */
    expression_t *object;         /**< Receiver. */
    expression_t **indices;       /**< Indices in source order. */
    size_t count;                 /**< Number of indices. */
} index_access_t;

/** @brief Creates an index expression shell before its indices are reduced. */
node_t *create_index_access_node(arena_t *arena, expression_t *object);
/** @brief Copies the completed index list into the graph arena. */
void set_index_arguments(node_t *node, arena_t *arena, expression_t **indices, size_t count);
/** @brief Emits a read, optionally retaining the receiver for a subsequent method call. */
instr_index_t
generate_index_read(node_t *node, code_builder_t *code, data_builder_t *data, bool retain_receiver);
