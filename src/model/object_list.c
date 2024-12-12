/**
 * @file object_list.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of functions for managing object lists in Goat language.
 */

#include "object_list.h"

void init_object_list(object_list_t *list) {
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}

void add_object_to_list(object_list_t *list, object_t *obj) {
    if (list->tail) {
        list->tail->next = obj;
        obj->previous = list->tail;
        list->tail = obj;
    } else {
        list->head = list->tail = obj;
    }
    list->size++;
}

void remove_object_from_list(object_list_t *list, object_t *obj) {
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
    obj->previous = obj->next = NULL;
    list->size--;
}

object_t *remove_first_object_from_list(object_list_t *list) {
    object_t *obj = list->head;
    if (obj == NULL) {
        return NULL;
    }
    list->head = obj->next;
    if (obj->next) {
        obj->next->previous = NULL;
    } else {
        list->tail = NULL;
    }
    list->size--;
    return obj;
}
