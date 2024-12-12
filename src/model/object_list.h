/**
 * @file object_list.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Defines the object list structure for Goat language.
 * 
 * This file describes the structure for a doubly linked list that holds objects
 * in the Goat programming language. The list allows for efficient traversal 
 * and manipulation of objects in both directions. Each object can belong to 
 * one such list, enabling bidirectional linkage between objects.
 */

#pragma once

#include <stddef.h>

#include "object.h"

/**
 * @typedef object_list_t
 * @brief Forward declaration for the object list structure.
 */
typedef struct object_list_t object_list_t;

/**
 * @struct object_list_t
 * @brief Structure representing a doubly linked list of objects.
 * 
 * This structure defines a doubly linked list used to store objects in the 
 * Goat language. The list supports bidirectional traversal, where each object 
 * can point to its previous and next objects in the list.
 */
struct object_list_t {
    /**
     * @brief Pointer to the first object in the list.
     */
    object_t *head;

    /**
     * @brief Pointer to the last object in the list.
     */
    object_t *tail;

    /**
     * @brief The number of objects in the list.
     */
    size_t size;
};

/**
 * @brief Initializes a new empty object list.
 * 
 * This function initializes an empty object list by setting both the head 
 * and tail pointers to NULL, and the size to 0.
 * 
 * @param list The object list to initialize.
 */
void init_object_list(object_list_t *list);

/**
 * @brief Adds an object to the end of the list.
 * 
 * This function adds the specified object to the end of the doubly linked list.
 * 
 * @param list The object list to which the object should be added.
 * @param obj The object to add to the list.
 */
void add_object_to_list(object_list_t *list, object_t *obj);

/**
 * @brief Removes an object from the list.
 * 
 * This function removes the specified object from the list. The object is 
 * unlinked from the list, and the size is updated.
 * 
 * @param list The object list from which to remove the object.
 * @param obj The object to remove from the list.
 */
void remove_object_from_list(object_list_t *list, object_t *obj);

/**
 * @brief Removes and returns the first object from the given list.
 * 
 * This function removes the first object from the list and returns it. If the list 
 * is empty, it returns `NULL`. The list is updated to reflect the removal of the object.
 * 
 * @param list The list from which the object is removed.
 * @return A pointer to the first object in the list, or `NULL` if the list is empty.
 */
object_t *remove_first_object_from_list(object_list_t *list);
