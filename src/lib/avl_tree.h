/**
 * @file avl_tree.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definitions of structures and function prototypes for an AVL tree.
 */

#pragma once

#include "value.h"

#include <stdbool.h>

typedef struct arena_t arena_t;

/** @brief A node in the AVL tree. */
typedef struct avl_node_t {
    void *key;                /**< Pointer to the key stored in the node. */
    value_t value;            /**< Value associated with the key. */
    int height;               /**< Height of the node, used for balancing. */
    struct avl_node_t *left;  /**< Pointer to the left child node. */
    struct avl_node_t *right; /**< Pointer to the right child node. */
} avl_node_t;

/** @brief The AVL tree structure. */
typedef struct {
    /** @brief Pointer to the root node of the tree. */
    avl_node_t *root;

    /** @brief Comparator function to compare keys in the tree. */
    int (*comparator)(const void *, const void *);

    /**
     * @brief Optional key copy function.
     *
     * If set, this function is used to copy keys before storing them in the tree during insertion
     * and cloning. If NULL, keys are copied shallowly.
     */
    void *(*copy_key)(void *key);

    /**
     * @brief Optional value copy function.
     *
     * If set, this function is used to copy values before storing them in the tree during
     * insertion, update, and cloning. If NULL, values are copied shallowly.
     */
    value_t (*copy_value)(value_t value);

    /**
     * @brief Optional key destroy function.
     *
     * If NULL, keys are not destroyed by the tree.
     */
    void (*destroy_key)(void *key);

    /**
     * @brief Optional value destroy function.
     *
     * If NULL, values are not destroyed by the tree.
     */
    void (*destroy_value)(value_t value);
} avl_tree_t;

/**
 * @brief AVL tree that stores its own arena and embeds avl_tree_t as the first field.
 *
 * Memory is not freed individually; it lives for the arena's lifetime.
 */
typedef struct {
    /** @brief Embedded plain AVL tree. */
    avl_tree_t base;

    /** @brief Memory arena used for allocations. */
    arena_t *arena;
} avl_tree_arena_t;

/** @brief Creates an empty heap-owned AVL tree with the supplied ordering. */
avl_tree_t *create_avl_tree(int (*comparator)(const void *, const void *));

/** @brief Creates an empty AVL tree that allocates from the given arena. */
avl_tree_arena_t *create_avl_tree_arena(arena_t *arena,
                                        int (*comparator)(const void *, const void *));

/**
 * @brief Inserts or replaces a value using the tree copy callbacks.
 * Returns the old value unless destroy_value consumes it; otherwise returns zero.
 * New keys are copied only on insertion.
 */
value_t set_in_avl_tree(avl_tree_t *tree, void *key, value_t value);

/** @brief Arena-backed variant of set_in_avl_tree(), with the same copy/destruction rules. */
value_t set_in_avl_tree_arena(avl_tree_arena_t *tree, void *key, value_t value);

/**
 * @brief Checks if the AVL tree contains a node with the specified key.
 * @return `true` if the tree contains a node with the specified key, otherwise `false`.
 */
bool avl_tree_contains(const avl_tree_t *tree, const void *key);

/**
 * @brief Retrieves the value associated with the specified key in the AVL tree.
 * @return The value associated with the specified key, or default value (filled with zeros) if the
 * key is not found in the tree.
 */
value_t get_from_avl_tree(const avl_tree_t *tree, const void *key);

/** @brief Applies a function to each key-value pair in the AVL tree, with user data. */
void avl_tree_for_each(const avl_tree_t *tree,
                       void (*func)(void *user_data, void *key, value_t value),
                       void *user_data);

/**
 * @brief Clones an AVL tree preserving its exact shape and node heights.
 *
 * The clone is built in O(N) time without reinserting keys and without rebalancing.
 * @return Newly allocated AVL tree clone, or NULL if source tree is NULL.
 */
avl_tree_t *clone_avl_tree(const avl_tree_t *tree);

/**
 * @brief Clones an AVL tree into the given arena preserving exact shape and node heights.
 *
 * The clone is built in O(N) time without reinserting keys and without rebalancing.
 * @return Newly allocated arena-backed AVL tree clone, or NULL if source tree is NULL.
 */
avl_tree_arena_t *clone_avl_tree_arena(arena_t *arena, const avl_tree_t *tree);

/**
 * @brief Clears all nodes in the AVL tree without deallocating the tree structure.
 *
 * After this function is called, the tree will be empty but still valid for further operations.
 */
void clear_avl_tree(avl_tree_t *tree);

/**
 * @brief Destroys the AVL tree and frees all allocated memory.
 *
 * After this function is called, the tree is no longer usable, and all memory associated with it is
 * deallocated.
 */
void destroy_avl_tree(avl_tree_t *tree);
