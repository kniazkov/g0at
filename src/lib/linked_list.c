/**
 * @file linked_list.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of a doubly linked list using arena allocation.
 *
 * This file provides the function definitions for creating and modifying
 * a doubly linked list where memory is managed by a memory arena. Nodes store
 * `value_t` items and support adding elements to the front or back.
 */

#include "arena.h"
#include "linked_list.h"

static list_item_t *create_list_item(list_t *list, value_t value) {
    list_item_t *node = (list_item_t *)alloc_zeroed_from_arena(
        list->arena,
        sizeof(list_item_t)
    );
    node->value = value;
    return node;
}

list_t *create_linked_list(arena_t *arena) {
    list_t *list = (list_t *)alloc_zeroed_from_arena(arena, sizeof(list_t));
    list->arena = arena;
    return list;
}

void prepend_item_to_linked_list(list_t *list, value_t value) {
    list_item_t *node = create_list_item(list, value);
    node->next = list->head;
    if (list->head)
        list->head->prev = node;
    else
        list->tail = node;
    list->head = node;
    list->size++;
}

void append_item_to_linked_list(list_t *list, value_t value) {
    list_item_t *node = create_list_item(list, value);
    node->value = value;
    node->prev = list->tail;
    if (list->tail)
        list->tail->next = node;
    else
        list->head = node;
    list->tail = node;
    list->size++;
}

void insert_item_to_linked_list_before_existing(list_t *list, list_item_t *before,
        value_t value) {
    if (!before) {
        append_item_to_linked_list(list, value);
        return;
    }

    list_item_t *node = create_list_item(list, value);
    node->prev = before->prev;
    node->next = before;
    if (before->prev) {
        before->prev->next = node;
    } else {
        list->head = node;
    }
    before->prev = node;
    list->size++;
}

void remove_item_from_linked_list(list_t *list, list_item_t *item) {
    if (!list || !item) {
        return;
    }
    if (item->prev) {
        item->prev->next = item->next;
    } else {
        list->head = item->next;
    }
    if (item->next) {
        item->next->prev = item->prev;
    } else {
        list->tail = item->prev;
    }
    list->size--;
    item->prev = NULL;
    item->next = NULL;
}
