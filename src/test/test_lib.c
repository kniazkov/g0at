/**
 * @file test_lib.c
 * @copyright 2025 Ivan Kniazkov
 * @brief A set of tests for testing extensions to the С standard library.
 */

#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#include "test_lib.h"
#include "test_macro.h"
#include "lib/allocate.h"
#include "lib/avl_tree.h"
#include "lib/vector.h"
#include "lib/string_ext.h"

bool test_memory_allocation() {
    int i;
    int32_t *array = (int32_t *)CALLOC(sizeof(int32_t) * 16);
    for (i = 0; i < 16; i++) {
        ASSERT(array[i] == 0);
    }
    ASSERT(get_allocated_memory_size() == sizeof(int32_t) * 16);
    FREE(array);
    ASSERT(get_allocated_memory_size() == 0);
    return true;
}

/**
 * @brief Comparator function for wide strings (wchar_t*).
 * 
 * This function compares two wide-character strings (`wchar_t *`) using the `wcscmp` function,
 * making it suitable for use as a comparator in an AVL tree.
 * 
 * @param first A pointer to the first wide-character string.
 * @param second A pointer to the second wide-character string.
 * 
 * @return A negative value if `first` is less than `second`, 
 *  zero if they are equal, and a positive value if `first` is greater than `second`.
 */
static int wcscmp_comparator(void *first, void *second) {
    return wcscmp((wchar_t *)first, (wchar_t *)second);
}

/**
 * @brief Callback function for iterating over the AVL tree.
 * 
 * This function is used as a callback for `avl_tree_for_each`. It adds each key from the AVL
 * tree to the provided vector.
 * 
 * @param user_data A pointer to user-specific data, in this case, a pointer to a vector.
 * @param key A pointer to the key of the current node in the AVL tree.
 * @param value Value associated with the key in the AVL tree.
 * 
 * @note The function adds each key to the vector passed via `user_data`.
 */
static void avl_callback(void *user_data, void *key, value_t value) {
    vector_t *keys = (vector_t *)user_data;
    append_to_vector(keys, WSTRDUP((wchar_t *)key));
}

bool test_avl_tree() {
    avl_tree_t *tree = create_avl_tree(wcscmp_comparator);
    set_in_avl_tree(tree, L"gamma", (value_t){.ptr = L"third"});
    value_t previous = set_in_avl_tree(tree, L"alpha", (value_t){.ptr = L"first"});
    ASSERT(previous.ptr == NULL);
    set_in_avl_tree(tree, L"beta", (value_t){.ptr = L"second"});
    ASSERT(avl_tree_contains(tree, L"alpha"));
    ASSERT(false == avl_tree_contains(tree, L"delta"));
    vector_t *keys = create_vector();
    avl_tree_for_each(tree, avl_callback, keys);
    ASSERT(keys->size == 3);
    ASSERT(wcscmp(L"alpha", (wchar_t *)keys->data[0]) == 0);
    ASSERT(wcscmp(L"beta", (wchar_t *)keys->data[1]) == 0);
    ASSERT(wcscmp(L"gamma", (wchar_t *)keys->data[2]) == 0);
    destroy_vector_ex(keys, FREE);
    ASSERT(wcscmp(L"first", (wchar_t *)get_from_avl_tree(tree, L"alpha").ptr) == 0);
    ASSERT(wcscmp(L"second", (wchar_t *)get_from_avl_tree(tree, L"beta").ptr) == 0);
    ASSERT(wcscmp(L"third", (wchar_t *)get_from_avl_tree(tree, L"gamma").ptr) == 0);
    ASSERT(get_from_avl_tree(tree, L"delta").ptr == NULL);
    previous = set_in_avl_tree(tree, L"alpha", (value_t){.ptr = L"primary"});
    ASSERT(wcscmp(L"first", previous.ptr) == 0);
    ASSERT(wcscmp(L"primary", (wchar_t *)get_from_avl_tree(tree, L"alpha").ptr) == 0);
    destroy_avl_tree(tree);
    return true;
}

bool test_string_builder() {
    string_builder_t sb;
    init_string_builder(&sb);
    wchar_t *result = append_string(&sb, L"it");
    ASSERT(wcscmp(L"it", result) == 0);
    result = append_char(&sb, L' ');
    ASSERT(wcscmp(L"it ", result) == 0);
    result = append_string(&sb, L"works");
    ASSERT(wcscmp(L"it works", result) == 0);
    FREE(result);
    return true;
}
