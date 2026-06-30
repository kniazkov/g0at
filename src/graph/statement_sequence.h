/**
 * @file statement_sequence.h
 * @copyright 2026 Ivan Kniazkov
 * @brief ...
 * 
 */

#pragma once

#include <stdbool.h>

/**
 * ...
 */
typedef struct list_t list_t;

/**
 * ...
 */
typedef struct node_t node_t;

/**
 * 
 */
bool insert_statement_to_list_before(list_t *list, node_t *new_child, node_t *before_child);
