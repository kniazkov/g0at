/**
 * @file object_list.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of functions for managing object lists in Goat language.
 */

#include <assert.h>

#include "object_list.h"

void object_list_init(object_list_t *list) {
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}

void object_list_add(object_list_t *list, object_t *obj) {
    assert(obj->list == NULL);
    obj->list = list;
    if (list->tail) {
        list->tail->next = obj;
        obj->previous = list->tail;
        list->tail = obj;
    } else {
        list->head = list->tail = obj;
    }
    list->size++;
}

void object_list_remove(object_list_t *list, object_t *obj) {
    assert(obj->list == list);
    if (obj->previous) {
        obj->previous->next = obj->next;
    } else {
        list->head = obj->next;
    }
    if (obj->next) {
        obj->next->previous = obj->previous;
    } else {
        list->tail = obj->previous;
    }
    obj->list = NULL;
    obj->previous = obj->next = NULL;
    list->size--;
}
