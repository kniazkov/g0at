/**
 * @file avl_tree.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definitions of structures and function prototypes for an AVL tree.
 *
 * An AVL tree is a self-balancing binary search tree where the difference in heights between
 * the left and right subtrees of any node is at most one. This structure is generic and allows
 * for flexible use with any data type through the use of void pointers and a custom comparator
 * function.
 */

#pragma once

/**
 * @struct avl_node_t
 * @brief A node in the AVL tree.
 *
 * This structure represents a single node in the AVL tree, which contains a key-value pair,
 * height information, and pointers to its left and right children.
 *
 * The `key` and `value` are stored as `void *`, which allows the tree to store any type of data.
 * The `height` is used to maintain the balance of the tree, ensuring that operations on the tree
 * can be performed in logarithmic time.
 */
typedef struct avl_node_t {
    void *key;                /**< Pointer to the key stored in the node. */
    void *value;              /**< Pointer to the value associated with the key. */
    int height;               /**< Height of the node, used for balancing. */
    struct avl_node_t *left;  /**< Pointer to the left child node. */
    struct avl_node_t *right; /**< Pointer to the right child node. */
} avl_node_t;

/**
 * @struct avl_tree_t
 * @brief The AVL tree structure.
 *
 * This structure represents the AVL tree itself, which consists of a root node and a comparator
 * function. The comparator function is used to compare keys and determine the order of nodes
 * within the tree.
 */
typedef struct avl_tree_t {
    avl_node_t *root;                /**< Pointer to the root node of the tree. */
    int (*comparator)(void*, void*); /**< Comparator function to compare keys in the tree. */
} avl_tree_t;
