/**
 * @file linked_list.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of a doubly linked list using arena allocation.
 *
 * This file provides the function definitions for creating and modifying
 * a doubly linked list where memory is managed by a memory arena. Nodes store
 * generic `void*` data and support adding elements to the front or back.
 */

#include "linked_list.h"

list_t *create_linked_list(arena_t *arena) {
    return (list_t *)alloc_zeroed_from_arena(arena, sizeof(list_t));
}

void list_push_front(list_t *list, arena_t *arena, void *data) {
    list_item_t *node = (list_item_t *)alloc_zeroed_from_arena(
        arena,
        sizeof(list_item_t)
    );
    node->data = data;
    node->next = list->head;
    if (list->head)
        list->head->prev = node;
    else
        list->tail = node;
    list->head = node;
    list->size++;
}

void list_push_back(list_t *list, arena_t *arena, void *data) {
    list_item_t *node = (list_item_t *)alloc_zeroed_from_arena(
        arena,
        sizeof(list_item_t)
    );
    node->data = data;
    node->prev = list->tail;
    if (list->tail)
        list->tail->next = node;
    else
        list->head = node;
    list->tail = node;
    list->size++;
}
