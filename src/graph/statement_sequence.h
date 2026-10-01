/**
 * @file statement_sequence.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Helpers for working with ordered lists of statement nodes.
 */

#pragma once

#include "lib/linked_list.h"
#include "lib/string_ext.h"
#include "lib/value.h"

typedef struct node_t node_t;

/** @brief Inserts a statement before before_child; returns false if insertion is invalid. */
bool insert_statement_to_list_before(list_t *list, node_t *new_child, node_t *before_child);

/** @brief Generates compact Goat source code from a list of statements. */
string_value_t generate_goat_code_from_statement_list(list_t *list,
        string_builder_t *builder, bool brackets);
