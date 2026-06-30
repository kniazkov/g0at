/**
 * @file statement_sequence.h
 * @copyright 2026 Ivan Kniazkov
 * @brief ...
 * 
 */

#pragma once

#include "lib/linked_list.h"
#include "lib/string_ext.h"
#include "lib/value.h"

/**
 * ...
 */
typedef struct node_t node_t;

/**
 * ...
 */
bool insert_statement_to_list_before(list_t *list, node_t *new_child, node_t *before_child);


/**
 * ...
 */
string_value_t generate_goat_code_from_statement_list(list_t *list,
        string_builder_t *builder, bool brackets);
