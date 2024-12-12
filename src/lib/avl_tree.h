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

#include <stdbool.h>

#include "value.h"

/**
 * @struct avl_node_t
 * @brief A node in the AVL tree.
 *
 * This structure represents a single node in the AVL tree, which contains a key-value pair,
 * height information, and pointers to its left and right children.
 *
 * The `key` is stored as a `void *`, which allows the tree to store any type of data.
 * The `value` is stored as a `value_t`, which can either hold a `void *` pointer to any type
 * of data or an primitive type.
 * The `height` is used to maintain the balance of the tree, ensuring that operations on the tree
 * can be performed in logarithmic time.
 */
typedef struct avl_node_t {
    void *key;                /**< Pointer to the key stored in the node. */
    value_t value;            /**< Value associated with the key. */
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
typedef struct {
    avl_node_t *root;                /**< Pointer to the root node of the tree. */
    int (*comparator)(void*, void*); /**< Comparator function to compare keys in the tree. */
} avl_tree_t;

/**
 * @brief Creates an empty AVL tree.
 * 
 * This function allocates memory for an AVL tree structure, initializes it with
 * the provided comparator function, and sets the root pointer to NULL, as the tree
 * is initially empty.
 * 
 * The comparator function is used to compare keys within the tree, determining the
 * order of nodes. The tree's memory is managed using the ALLOC function, which ensures
 * that memory allocation failures are handled by terminating the program.
 * 
 * @param comparator A pointer to the comparator function that compares keys in the tree.
 *                   The function should return a negative value if the first key is less,
 *                   zero if they are equal, and a positive value if the first key is greater.
 * 
 * @return A pointer to the newly created AVL tree structure. The root of the tree is
 *  initially set to NULL.
 */
avl_tree_t *create_avl_tree(int (*comparator)(void*, void*));

/**
 * @brief Inserts a key-value pair into the AVL tree or updates the value if the key exists.
 * 
 * This function attempts to insert a new key-value pair into the AVL tree. If the key already
 * exists in the tree, the function updates the corresponding value and returns the old value.
 * If the key does not exist, the function inserts the new pair and returns zeros.
 * 
 * The function uses the AVL tree's comparator to find the correct position of the key.
 * The tree is balanced after the insertion or update to ensure optimal search times.
 * 
 * @param tree A pointer to the AVL tree where the key-value pair is inserted or updated.
 * @param key A pointer to the key to insert or update in the tree.
 * @param value Value associated with the key.
 * 
 * @return If the key already exists, the function returns the old value associated with the key.
 *  If the key is new, the function returns default value (filled with zeros).
 */
value_t set_in_avl_tree(avl_tree_t *tree, void *key, value_t value);

/**
 * @brief Checks if the AVL tree contains a node with the specified key.
 * 
 * This function searches the AVL tree for a node with the given key. It uses
 * the tree's comparator function to compare the key with the nodes in the tree.
 * If a node with the specified key is found, the function returns `true`; otherwise,
 * it returns `false`.
 * 
 * @param tree A pointer to the AVL tree to search.
 * @param key A pointer to the key to search for in the tree.
 * 
 * @return `true` if the tree contains a node with the specified key, otherwise `false`.
 */
bool avl_tree_contains(avl_tree_t *tree, void *key);

/**
 * @brief Retrieves the value associated with the specified key in the AVL tree.
 * 
 * This function searches the AVL tree for a node containing the specified key. If the key
 * is found, the function returns the corresponding value. If the key is not found, the function
 * returns zeros.
 * 
 * @param tree A pointer to the AVL tree to search.
 * @param key A pointer to the key whose associated value is to be retrieved.
 * 
 * @return The value associated with the specified key, or default value (filled with zeros)
 *  if the key is not found in the tree.
 */
value_t get_from_avl_tree(avl_tree_t *tree, void *key);

/**
 * @brief Applies a function to each key-value pair in the AVL tree, with user data.
 * 
 * This function performs an in-order traversal of the AVL tree, applying the provided
 * function to each key-value pair. The function is called for each node in the tree
 * with the key, value, and user data as arguments. This allows the user to pass
 * additional context (user data) to the callback function.
 * 
 * @param tree A pointer to the AVL tree to traverse.
 * @param func A pointer to the function to apply to each key-value pair. The function
 *  should have the signature `void func(void* user_data, void* key, value_t value)`.
 * @param user_data A pointer to user data that will be passed to the callback function.
 */
void avl_tree_for_each(avl_tree_t *tree,
    void (*func)(void* user_data, void* key, value_t value), void *user_data);

/**
 * @brief Destroys the AVL tree and frees all allocated memory.
 * 
 * This function recursively frees all nodes in the AVL tree and then frees the memory
 * allocated for the tree structure itself. After this function is called, the tree is no longer
 * usable, and all memory associated with it is deallocated.
 * 
 * @param tree A pointer to the AVL tree to destroy.
 */
void destroy_avl_tree(avl_tree_t *tree);
