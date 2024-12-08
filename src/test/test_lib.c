/**
 * @file test_lib.h
 * @copyright 2025 Ivan Kniazkov
 * @brief A set of tests for testing extensions to the С standard library
 */

#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#include "test_lib.h"
#include "test_macro.h"
#include "lib/allocate.h"
#include "lib/avl_tree.h"

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

static int wcscmp_comparator(void *first, void *second) {
    return wcscmp((wchar_t *)first, (wchar_t *)second);
}

bool test_avl_tree() {
    avl_tree_t *tree = avl_tree_create(wcscmp_comparator);
    avl_tree_set(tree, L"gamma", L"third");
    avl_tree_set(tree, L"alpha", L"first");
    avl_tree_set(tree, L"beta", L"second");
    ASSERT(avl_tree_contains(tree, L"alpha"));
    avl_tree_destroy(tree);
    return true;
}
