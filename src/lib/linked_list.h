/**
 * @file linked_list.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Doubly linked list implementation using arena allocation.
 *
 * This file defines the interface for a minimal doubly linked list where nodes
 * are allocated from a memory arena. The list stores `value_t` items.
 * Memory is never freed individually — the entire list is released when the arena is destroyed.
 */

#pragma once

#include "value.h"

/**
 * @brief Forward declaration for linked list node.
 */
typedef struct list_item_t list_item_t;

/**
 * @struct arena_t
 * @brief Forward declaration for memory arena structure.
 */
typedef struct arena_t arena_t;

/**
 * @struct list_item_t
 * @brief A node in a doubly linked list.
 */
struct list_item_t {
    list_item_t *prev; /**< Pointer to the previous node. */
    list_item_t *next; /**< Pointer to the next node. */
    value_t value;     /**< Stored data. */
};

/**
 * @struct list_t
 * @brief A doubly linked list using arena allocation.
 */
typedef struct {
    list_item_t *head; /**< Pointer to the first node. */
    list_item_t *tail; /**< Pointer to the last node. */
    size_t size;       /**< Size of the list */
} list_t;

/**
 * @brief Creates a new empty linked list.
 * 
 * @param arena Memory arena used for allocation.
 * @return A pointer to a newly created list.
 */
list_t *create_linked_list(arena_t *arena);

/**
 * @brief Adds a new element to the front of the list.
 * 
 * @param list The list to modify.
 * @param arena Memory arena used for allocation.
 * @param value Data to store.
 */
void list_push_front(list_t *list, arena_t *arena, value_t value);

/**
 * @brief Adds a new element to the end of the list.
 * 
 * @param list The list to modify.
 * @param arena Memory arena used for allocation.
 * @param value Data to store.
 */
void list_push_back(list_t *list, arena_t *arena, value_t value);
