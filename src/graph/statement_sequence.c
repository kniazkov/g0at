/**
 * @file statement_sequence.h
 * @copyright 2026 Ivan Kniazkov
 * @brief ...
 * 
 */

#include "statement_sequence.h"
#include "node.h"
#include "lib/linked_list.h"

bool insert_statement_to_list_before(list_t *list, node_t *new_child, node_t *before_child) {
    if (!is_statement(new_child->vtbl->type)) {
        return false;
    }
    list_item_t *item = list->head;
    while (item) {
        if (item->value.ptr == before_child) {
            break;
        }
        item = item->next;
    }
    if (!item) {
        return false;
    }
    insert_item_to_linked_list_before_existing(
        list,
        item,
        (value_t){ .ptr = new_child }
    );
    return true;
}
