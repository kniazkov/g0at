/**
 * @file object_list.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the object list structure for Goat language.
 */

#pragma once

#include <stddef.h>

#include "object.h"

typedef struct object_list_t object_list_t;

/** @brief A doubly linked list of objects. */
struct object_list_t {
    /** @brief Pointer to the first object in the list. */
    object_t *head;

    /** @brief Pointer to the last object in the list. */
    object_t *tail;

    /** @brief The number of objects in the list. */
    size_t size;
};

/**
 * @brief Initializes a new empty object list.
 *
 * Initializes an empty object list by setting both the head and tail pointers to NULL, and the size
 * to 0.
 */
void init_object_list(object_list_t *list);

/** @brief Adds an object to the end of the list. */
void add_object_to_list(object_list_t *list, object_t *obj);

/** @brief Removes an object from the list. */
void remove_object_from_list(object_list_t *list, object_t *obj);

/**
 * @brief Removes and returns the first object from the given list.
 *
 * If the list is empty, it returns `NULL`.
 * @return A pointer to the first object in the list, or `NULL` if the list is empty.
 */
object_t *remove_first_object_from_list(object_list_t *list);
