/**
 * @file avl_tree.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementations of functions that work with AVL tree.
 */

#include "avl_tree.h"

#include "lib/allocate.h"
#include "lib/arena.h"

#include <memory.h>

/** @brief Returns the larger of two integers. */
static inline int max(int a, int b) {
    return a > b ? a : b;
}

/**
 * @brief Gets the height of a node.
 *
 * If the node is NULL, it returns 0.
 */
static inline int get_height(avl_node_t *node) {
    return node ? node->height : 0;
}

/** @brief Calculates the balance factor of a node. */
static inline int get_balance(avl_node_t *node) {
    return node ? get_height(node->left) - get_height(node->right) : 0;
}

/** @brief Copies a key according to tree ownership rules. */
static inline void *copy_key(const avl_tree_t *tree, void *key) {
    return tree->copy_key ? tree->copy_key(key) : key;
}

/** @brief Copies a value according to tree ownership rules. */
static inline value_t copy_value(const avl_tree_t *tree, value_t value) {
    return tree->copy_value ? tree->copy_value(value) : value;
}

/**
 * @brief Performs a left rotation on the given node.
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

/** @brief Balances the AVL tree at the given node. */
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
 * @brief Inserts or replaces a value, then restores AVL balance.
 * Replaced values are destroyed through the callback, or returned through old_value.
 */
static avl_node_t *
insert(avl_tree_t *tree, avl_node_t *node, void *key, value_t value, value_t *old_value) {
    if (!node) {
        avl_node_t *new_node = (avl_node_t *)CALLOC(sizeof(avl_node_t));
        new_node->key = copy_key(tree, key);
        new_node->value = copy_value(tree, value);
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
        if (tree->destroy_value) {
            tree->destroy_value(node->value);
            memset(old_value, 0, sizeof(value_t));
        } else {
            *old_value = node->value;
        }
        node->value = copy_value(tree, value);
        return node;
    }

    node->height = 1 + max(get_height(node->left), get_height(node->right));

    return balance(tree, node);
}

/**
 * @brief Arena-backed insertion helper.
 * @return The node where the key-value pair was inserted or `NULL` if the key already exists.
 */
static avl_node_t *insert_arena(avl_tree_arena_t *tree,
                                avl_node_t *node,
                                void *key,
                                value_t value,
                                value_t *old_value) {
    if (!node) {
        avl_node_t *new_node =
            (avl_node_t *)alloc_zeroed_from_arena(tree->arena, sizeof(avl_node_t));
        new_node->key = copy_key(&tree->base, key);
        new_node->value = copy_value(&tree->base, value);
        new_node->height = 1;
        memset(old_value, 0, sizeof(value_t));
        return new_node;
    }

    int cmp = tree->base.comparator(key, node->key);

    if (cmp < 0) {
        node->left = insert_arena(tree, node->left, key, value, old_value);
    } else if (cmp > 0) {
        node->right = insert_arena(tree, node->right, key, value, old_value);
    } else {
        if (tree->base.destroy_value) {
            tree->base.destroy_value(node->value);
            memset(old_value, 0, sizeof(value_t));
        } else {
            *old_value = node->value;
        }
        node->value = copy_value(&tree->base, value);
        ;
        return node;
    }

    node->height = 1 + max(get_height(node->left), get_height(node->right));

    return balance(&tree->base, node);
}

/**
 * @brief Recursively searches for a node with the specified key in the AVL tree.
 * @return A pointer to the node with the specified key, or `NULL` if no such node is found.
 */
static avl_node_t *find(const avl_tree_t *tree, avl_node_t *node, const void *key) {
    if (!node)
        return NULL;

    int cmp = tree->comparator(key, node->key);

    if (cmp < 0) {
        return find(tree, node->left, key);
    } else if (cmp > 0) {
        return find(tree, node->right, key);
    } else {
        return node;
    }
}

/** @brief Recursively performs an in-order traversal of the AVL tree. */
static void inorder_traversal(avl_node_t *node,
                              void (*func)(void *user_data, void *key, value_t value),
                              void *user_data) {
    if (node) {
        inorder_traversal(node->left, func, user_data);
        func(user_data, node->key, node->value);
        inorder_traversal(node->right, func, user_data);
    }
}

/**
 * @brief Recursively destroys all nodes in the AVL tree.
 * `tree`: AVL tree that owns the nodes.
 */
static void destroy_nodes(avl_tree_t *tree, avl_node_t *node) {
    if (node) {
        destroy_nodes(tree, node->left);
        destroy_nodes(tree, node->right);
        if (tree->destroy_key) {
            tree->destroy_key(node->key);
        }
        if (tree->destroy_value) {
            tree->destroy_value(node->value);
        }
        FREE(node);
    }
}

/**
 * @brief Recursively clones nodes preserving the exact tree shape.
 *
 * This does not insert nodes through AVL logic, so it runs in O(N) and does not rebalance.
 */
static avl_node_t *clone_nodes(const avl_tree_t *tree, const avl_node_t *node) {
    if (!node) {
        return NULL;
    }
    avl_node_t *copy = (avl_node_t *)CALLOC(sizeof(avl_node_t));
    copy->key = copy_key(tree, node->key);
    copy->value = copy_value(tree, node->value);
    copy->height = node->height;
    copy->left = clone_nodes(tree, node->left);
    copy->right = clone_nodes(tree, node->right);
    return copy;
}

/**
 * @brief Recursively clones nodes into an arena preserving the exact tree shape.
 *
 * This does not insert nodes through AVL logic, so it runs in O(N) and does not rebalance.
 */
static avl_node_t *
clone_nodes_arena(arena_t *arena, const avl_tree_t *tree, const avl_node_t *node) {
    if (!node) {
        return NULL;
    }
    avl_node_t *copy = (avl_node_t *)alloc_zeroed_from_arena(arena, sizeof(avl_node_t));
    copy->key = copy_key(tree, node->key);
    copy->value = copy_value(tree, node->value);
    copy->height = node->height;
    copy->left = clone_nodes_arena(arena, tree, node->left);
    copy->right = clone_nodes_arena(arena, tree, node->right);
    return copy;
}

avl_tree_t *create_avl_tree(int (*comparator)(const void *, const void *)) {
    avl_tree_t *tree = (avl_tree_t *)CALLOC(sizeof(avl_tree_t));
    tree->root = NULL;
    tree->comparator = comparator;
    return tree;
}

avl_tree_arena_t *create_avl_tree_arena(arena_t *arena,
                                        int (*comparator)(const void *, const void *)) {
    avl_tree_arena_t *tree =
        (avl_tree_arena_t *)alloc_zeroed_from_arena(arena, sizeof(avl_tree_arena_t));
    tree->base.root = NULL;
    tree->base.comparator = comparator;
    tree->arena = arena;
    return tree;
}

value_t set_in_avl_tree(avl_tree_t *tree, void *key, value_t value) {
    value_t old_value;
    tree->root = insert(tree, tree->root, key, value, &old_value);
    return old_value;
}

value_t set_in_avl_tree_arena(avl_tree_arena_t *tree, void *key, value_t value) {
    value_t old_value;
    tree->base.root = insert_arena(tree, tree->base.root, key, value, &old_value);
    return old_value;
}

bool avl_tree_contains(const avl_tree_t *tree, const void *key) {
    avl_node_t *node = find(tree, tree->root, key);
    return node != NULL;
}

value_t get_from_avl_tree(const avl_tree_t *tree, const void *key) {
    value_t value = {0};
    avl_node_t *node = find(tree, tree->root, key);
    if (node) {
        value = node->value;
    }
    return value;
}

void avl_tree_for_each(const avl_tree_t *tree,
                       void (*func)(void *user_data, void *key, value_t value),
                       void *user_data) {
    if (tree && tree->root) {
        inorder_traversal(tree->root, func, user_data);
    }
}

avl_tree_t *clone_avl_tree(const avl_tree_t *tree) {
    if (!tree) {
        return NULL;
    }
    avl_tree_t *copy = (avl_tree_t *)ALLOC(sizeof(avl_tree_t));
    copy->comparator = tree->comparator;
    copy->copy_key = tree->copy_key;
    copy->copy_value = tree->copy_value;
    copy->destroy_key = tree->destroy_key;
    copy->destroy_value = tree->destroy_value;
    copy->root = clone_nodes(tree, tree->root);
    return copy;
}

avl_tree_arena_t *clone_avl_tree_arena(arena_t *arena, const avl_tree_t *tree) {
    if (!tree) {
        return NULL;
    }
    avl_tree_arena_t *copy =
        (avl_tree_arena_t *)alloc_zeroed_from_arena(arena, sizeof(avl_tree_arena_t));
    copy->base.comparator = tree->comparator;
    copy->base.copy_key = tree->copy_key;
    copy->base.copy_value = tree->copy_value;
    copy->base.destroy_key = tree->destroy_key;
    copy->base.destroy_value = tree->destroy_value;
    copy->base.root = clone_nodes_arena(arena, tree, tree->root);
    copy->arena = arena;
    return copy;
}

void clear_avl_tree(avl_tree_t *tree) {
    destroy_nodes(tree, tree->root);
    tree->root = NULL;
}

void destroy_avl_tree(avl_tree_t *tree) {
    if (tree) {
        destroy_nodes(tree, tree->root);
        FREE(tree);
    }
}
