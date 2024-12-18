/**
 * @file avl_tree.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementations of functions that work with AVL tree.
 *
 * An AVL tree is a self-balancing binary search tree where the difference in heights between
 * the left and right subtrees of any node is at most one. This structure is generic and allows
 * for flexible use with any data type through the use of void pointers and a custom comparator
 * function.
 */

#include <memory.h>

#include "avl_tree.h"
#include "lib/allocate.h"

/**
 * @brief Returns the larger of two integers.
 * @param a The first integer to compare.
 * @param b The second integer to compare.
 * @return The larger of the two integers.
 */
static inline int max(int a, int b) {
    return a > b ? a : b;
}

/**
 * @brief Gets the height of a node.
 * 
 * This function returns the height of a given node. If the node is NULL, it returns 0.
 * 
 * @param node The node whose height is to be determined.
 * @return The height of the node.
 */
static inline int get_height(avl_node_t *node) {
    return node ? node->height : 0;
}

/**
 * @brief Calculates the balance factor of a node.
 * 
 * This function calculates the balance factor of a node, which is the difference between
 * the height of the left and right subtrees. The balance factor helps to determine
 * whether the tree is balanced or requires rotation.
 * 
 * @param node The node whose balance factor is to be calculated.
 * @return The balance factor of the node.
 */
static inline int get_balance(avl_node_t *node) {
    return node ? get_height(node->left) - get_height(node->right) : 0;
}

/**
 * @brief Performs a left rotation on the given node.
 * 
 * This function performs a left rotation to balance the AVL tree. It is used when the right
 * subtree of a node is too heavy.
 * 
 * @param tree A pointer to the AVL tree structure.
 * @param node The node to rotate left.
 * 
 * @return The new root node after the rotation.
 */
static avl_node_t *rotate_left(avl_tree_t *tree, avl_node_t *node) {
    avl_node_t *new_root = node->right;
    node->right = new_root->left;
    new_root->left = node;
    node->height = 1 + max(get_height(node->left), get_height(node->right));
    new_root->height = 1 + max(get_height(new_root->left), get_height(new_root->right));
    return new_root;
}

/**
 * @brief Performs a right rotation on the given node.
 * 
 * This function performs a right rotation to balance the AVL tree. It is used when the left
 * subtree of a node is too heavy.
 * 
 * @param tree A pointer to the AVL tree structure.
 * @param node The node to rotate right.
 * 
 * @return The new root node after the rotation.
 */
static avl_node_t *rotate_right(avl_tree_t *tree, avl_node_t *node) {
    avl_node_t *new_root = node->left;
    node->left = new_root->right;
    new_root->right = node;
    node->height = 1 + max(get_height(node->left), get_height(node->right));
    new_root->height = 1 + max(get_height(new_root->left), get_height(new_root->right));
    return new_root;
}

/**
 * @brief Balances the AVL tree at the given node.
 * 
 * This function checks the balance factor of the node and performs the necessary rotations
 * to maintain the AVL property (balance factor between -1 and 1).
 * 
 * @param tree A pointer to the AVL tree structure.
 * @param node The node to balance.
 * 
 * @return The new root node of the balanced subtree.
 */
static avl_node_t *balance(avl_tree_t *tree, avl_node_t *node) {
    int balance_factor = get_balance(node);

    if (balance_factor > 1) {
        if (get_balance(node->left) < 0) {
            node->left = rotate_left(tree, node->left);
        }
        return rotate_right(tree, node);
    }

    if (balance_factor < -1) {
        if (get_balance(node->right) > 0) {
            node->right = rotate_right(tree, node->right);
        }
        return rotate_left(tree, node);
    }

    return node;
}

/**
 * @brief Inserts a key-value pair into the AVL tree.
 * 
 * This is a recursive function that attempts to insert the key-value pair into the tree.
 * If the key already exists, it does not insert a new pair. The function returns the node
 * where the insertion occurred or  `NULL` if the key is already in the tree.
 * 
 * After the insertion, the function ensures the tree is balanced by checking the heights
 * of nodes and performing rotations as necessary.
 * 
 * @param tree A pointer to the AVL tree structure.
 * @param node A pointer to the current node in the tree.
 * @param key A pointer to the key to insert.
 * @param value A pointer to the value associated with the key.
 * @param value A pointer to pointer to the previous value associated with the key.
 * 
 * @return The node where the key-value pair was inserted or `NULL` if the key already exists.
 */
static avl_node_t *insert(avl_tree_t *tree, avl_node_t *node, void *key,
        value_t value, value_t *old_value) {
    if (!node) {
        avl_node_t *new_node = (avl_node_t *)CALLOC(sizeof(avl_node_t));
        new_node->key = key;
        new_node->value = value;
        new_node->height = 1;
        memset(old_value, 0, sizeof(value_t));
        return new_node;
    }

    int cmp = tree->comparator(key, node->key);
    
    if (cmp < 0) {
        node->left = insert(tree, node->left, key, value, old_value);
    } else if (cmp > 0) {
        node->right = insert(tree, node->right, key, value, old_value);
    } else {
        *old_value = node->value;
        node->value = value;
        return node;
    }

    node->height = 1 + max(get_height(node->left), get_height(node->right));

    return balance(tree, node);
}

/**
 * @brief Recursively searches for a node with the specified key in the AVL tree.
 * 
 * This is a helper function that performs a recursive search to find the node
 * associated with the given key. The function uses the tree's comparator to compare
 * the key with each node in the tree.
 * 
 * @param tree A pointer to the AVL tree structure.
 * @param node The current node being examined.
 * @param key A pointer to the key to search for.
 * 
 * @return A pointer to the node with the specified key, or `NULL` if no such node is found.
 */
static avl_node_t *find(avl_tree_t *tree, avl_node_t *node, void *key) {
    if (!node) return NULL;

    int cmp = tree->comparator(key, node->key);
    
    if (cmp < 0) {
        return find(tree, node->left, key);
    } else if (cmp > 0) {
        return find(tree, node->right, key);
    } else {
        return node;
    }
}

/**
 * @brief Recursively performs an in-order traversal of the AVL tree.
 * 
 * This is a helper function that recursively traverses the AVL tree in an in-order
 * manner and applies the provided function to each key-value pair, passing the user data.
 * 
 * @param node The current node to process.
 * @param func A pointer to the function to apply to each key-value pair.
 * @param user_data A pointer to user data that will be passed to the callback function.
 */
static void inorder_traversal(avl_node_t *node,
        void (*func)(void* user_data, void* key, value_t value), void *user_data) {
    if (node) {
        inorder_traversal(node->left, func, user_data);
        func(user_data, node->key, node->value);
        inorder_traversal(node->right, func, user_data);
    }
}

/**
 * @brief Recursively destroys all nodes in the AVL tree.
 * 
 * This is a helper function that recursively traverses the AVL tree and frees all nodes.
 * 
 * @param node The current node to destroy.
 */
static void destroy_nodes(avl_node_t *node) {
    if (node) {
        destroy_nodes(node->left);
        destroy_nodes(node->right);
        FREE(node);
    }
}

avl_tree_t *create_avl_tree(int (*comparator)(void*, void*)) {
    avl_tree_t *tree = (avl_tree_t *)ALLOC(sizeof(avl_tree_t));
    tree->root = NULL; 
    tree->comparator = comparator;
    return tree;
}

value_t set_in_avl_tree(avl_tree_t *tree, void *key, value_t value) {
    value_t old_value;
    tree->root = insert(tree, tree->root, key, value, &old_value);
    return old_value;
}

bool avl_tree_contains(avl_tree_t *tree, void *key) {
    avl_node_t *node = find(tree, tree->root, key);
    return node != NULL;
}

value_t get_from_avl_tree(avl_tree_t *tree, void *key) {
    value_t value = {0};
    avl_node_t *node = find(tree, tree->root, key);
    if (node) {
        value = node->value;
    }
    return value;
}

void avl_tree_for_each(avl_tree_t *tree,
    void (*func)(void* user_data, void* key, value_t value), void *user_data) {
    if (tree && tree->root) {
        inorder_traversal(tree->root, func, user_data);
    }
}

void clear_avl_tree(avl_tree_t *tree) {
    destroy_nodes(tree->root);
    tree->root = NULL;
}

void destroy_avl_tree(avl_tree_t *tree) {
    if (tree) {
        destroy_nodes(tree->root);
        FREE(tree);
    }
}
