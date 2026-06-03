/**
 * @file linked_list.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Doubly linked list implementation using arena allocation.
 *
 * This file defines the interface for a minimal doubly linked list where nodes
 * are allocated from a memory arena. The list stores `value_t` items.
 * Memory is never freed individually — the entire list is released when the arena is destroyed.
 */

#pragma once

#include "value.h"

/**
 * @typedef list_t
 * @brief Forward declaration for linked list structure.
 */
typedef struct list_t list_t;

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
struct list_t {
    arena_t *arena;    /**< Memory arena used to allocate list nodes. */
    list_item_t *head; /**< Pointer to the first node. */
    list_item_t *tail; /**< Pointer to the last node. */
    size_t size;       /**< Size of the list */
};

/**
 * @brief Creates a new empty linked list.
 * 
 * The arena is stored inside the list and reused for all subsequent node allocations.
 * 
 * @param arena Memory arena used for allocation.
 * @return A pointer to a newly created list.
 */
list_t *create_linked_list(arena_t *arena);

/**
 * @brief Adds a new element to the front of the list.
 * 
 * @param list The list to modify.
 * @param value Data to store.
 */
void prepend_item_to_linked_list(list_t *list, value_t value);

/**
 * @brief Adds a new element to the end of the list.
 * 
 * @param list The list to modify.
 * @param value Data to store.
 */
void append_item_to_linked_list(list_t *list, value_t value);

/**
 * @brief Inserts a new element before an existing list item.
 *
 * Allocates a new list item from the list arena and inserts it immediately
 * before `before`. If `before` is the head item, the new item becomes the new
 * head. If `before` is NULL, the function appends the value to the end of the
 * list, because even linked lists deserve one tiny convenience.
 *
 * @param list The list to modify.
 * @param before Existing item before which the new value should be inserted,
 *        or NULL to append to the end.
 * @param value Data to store.
 */
void insert_item_to_linked_list_before_existing(list_t *list, list_item_t *before,
        value_t value);

/**
 * @brief Gets a value by its zero-based index.
 *
 * Traverses the list from the head and returns the value stored at the
 * specified index. If the index is out of range, returns a zero-initialized
 * value_t.
 *
 * @param list The list to read.
 * @param index Zero-based item index.
 * @return Stored value at the specified index, or zero value if index is out
 *         of range.
 */
value_t get_linked_list_value(const list_t *list, size_t index);

/**
 * @brief Removes a specific item from the list.
 *
 * Adjusts the neighboring nodes and list head/tail pointers so that the
 * specified item is no longer part of the list. The memory for the node is
 * not freed individually (because of arena allocation); it simply becomes
 * unreachable.
 *
 * @param list The list to modify (must not be NULL).
 * @param item The node to remove (must not be NULL and must belong to @p list).
 */
void remove_item_from_linked_list(list_t *list, list_item_t *item);

/**
 * @brief Clones a linked list into another arena.
 *
 * Creates a new linked list allocated from `arena` and copies all stored
 * values into newly allocated list items. The values themselves are copied
 * as-is; objects referenced by pointer values are not cloned.
 *
 * The resulting list is structurally independent from the source list: it has
 * its own list object and its own list items, even if the same arena is used.
 *
 * @param source Source list to clone.
 * @param arena Arena used to allocate the cloned list and its items.
 * @return Newly created independent list with the same values.
 */
list_t *clone_linked_list(const list_t *source, arena_t *arena);