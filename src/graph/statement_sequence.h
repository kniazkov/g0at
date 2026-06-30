/**
 * @file statement_sequence.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Helpers for working with ordered lists of statement nodes.
 */

#pragma once

#include "lib/linked_list.h"
#include "lib/string_ext.h"
#include "lib/value.h"

/**
 * @brief Forward declaration of a graph node.
 */
typedef struct node_t node_t;

/**
 * @brief Inserts a statement node into a statement list before another node.
 *
 * The function checks that @p new_child is a statement node and then searches
 * for @p before_child in @p list. If the target node is found, @p new_child is
 * inserted immediately before it.
 *
 * @param list The list of statement nodes to modify.
 * @param new_child The node to insert. Must be a statement node.
 * @param before_child The existing node before which @p new_child is inserted.
 * @return @c true if the node was inserted, @c false otherwise.
 */
bool insert_statement_to_list_before(list_t *list, node_t *new_child, node_t *before_child);


/**
 * @brief Generates compact Goat source code from a list of statements.
 *
 * Statements are appended to @p builder in their list order and separated by a
 * single space. If @p brackets is @c true, the generated sequence is wrapped in
 * braces. Empty lists are represented as either an empty string or an empty
 * braced block, depending on @p brackets and the current builder contents.
 *
 * @param list The list of statement nodes to generate code from.
 * @param builder The string builder used to accumulate the generated source.
 * @param brackets Whether the generated sequence should be wrapped in braces.
 * @return The resulting string value produced by the builder.
 */
string_value_t generate_goat_code_from_statement_list(list_t *list,
        string_builder_t *builder, bool brackets);
