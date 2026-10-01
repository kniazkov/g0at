/**
 * @file linked_list.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Doubly linked list implementation using arena allocation.
 */

#pragma once

#include "value.h"

typedef struct list_t list_t;

typedef struct list_item_t list_item_t;

typedef struct arena_t arena_t;

/** @brief A node in a doubly linked list. */
struct list_item_t {
    list_item_t *prev; /**< Pointer to the previous node. */
    list_item_t *next; /**< Pointer to the next node. */
    value_t value;     /**< Stored data. */
};

/** @brief A doubly linked list using arena allocation. */
struct list_t {
    arena_t *arena;    /**< Memory arena used to allocate list nodes. */
    list_item_t *head; /**< Pointer to the first node. */
    list_item_t *tail; /**< Pointer to the last node. */
    size_t size;       /**< Size of the list */
};

/** @brief Creates a new empty linked list. */
list_t *create_linked_list(arena_t *arena);

/** @brief Adds a new element to the front of the list. */
void prepend_item_to_linked_list(list_t *list, value_t value);

/** @brief Adds a new element to the end of the list. */
void append_item_to_linked_list(list_t *list, value_t value);

/**
 * @brief Inserts before an item; NULL before appends.
 * The item must belong to this list. Storage is owned by the list arena.
 */
void insert_item_to_linked_list_before_existing(list_t *list, list_item_t *before,
        value_t value);

/** @brief Returns the indexed value, or zero for a NULL list or out-of-range index. */
value_t get_linked_list_value(const list_t *list, size_t index);

/**
 * @brief Unlinks an item without freeing its arena storage.
 * NULL list/item is a no-op; a non-NULL item must belong to the list.
 */
void remove_item_from_linked_list(list_t *list, list_item_t *item);

/** @brief Copies list nodes into arena; stored values remain shallow copies. */
list_t *clone_linked_list(const list_t *source, arena_t *arena);