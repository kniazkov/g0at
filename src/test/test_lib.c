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
#include "lib/pair.h"

bool test_memory_allocation() {
    size_t allocated_before = get_allocated_memory_size();
    int i;
    int32_t *array = (int32_t *)CALLOC(sizeof(int32_t) * 16);
    for (i = 0; i < 16; i++) {
        ASSERT(array[i] == 0);
    }
    ASSERT(get_allocated_memory_size() - allocated_before == sizeof(int32_t) * 16);
    FREE(array);
    ASSERT(get_allocated_memory_size() - allocated_before == 0);
    return true;
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
    avl_tree_t *tree = create_avl_tree(string_comparator);
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
    init_string_builder(&sb, 0);
    string_value_t result = append_string(&sb, L"it");
    ASSERT(wcscmp(L"it", result.data) == 0);
    ASSERT(result.length == 2);
    result = append_char(&sb, L' ');
    ASSERT(wcscmp(L"it ", result.data) == 0);
    ASSERT(result.length == 3);
    result = append_string(&sb, L"works");
    ASSERT(wcscmp(L"it works", result.data) == 0);
    ASSERT(result.length == 8);
    FREE(result.data);
    return true;
}

bool test_binary_search() {
    pair_t pairs[] = {
        { L"fifth", L"five" },
        { L"first", L"one" },
        { L"fourth", L"four" },
        { L"second", L"two" },
        { L"third", L"three" }
    };
    ASSERT(wcscmp(L"one", (wchar_t *)binary_search(pairs, 5, L"first", string_comparator)) == 0);
    ASSERT(wcscmp(L"two", (wchar_t *)binary_search(pairs, 5, L"second", string_comparator)) == 0);
    ASSERT(wcscmp(L"three", (wchar_t *)binary_search(pairs, 5, L"third", string_comparator)) == 0);
    ASSERT(wcscmp(L"four", (wchar_t *)binary_search(pairs, 5, L"fourth", string_comparator)) == 0);
    ASSERT(wcscmp(L"five", (wchar_t *)binary_search(pairs, 5, L"fifth", string_comparator)) == 0);
    ASSERT(binary_search(pairs, 5, L"sixth", string_comparator) == NULL);
    return true;
}

bool test_double_to_string() {
    char buffer[32];
    double_to_string(1.0, buffer, sizeof(buffer));
    ASSERT(strcmp("1.0", buffer) == 0);
    double_to_string(-1.024, buffer, sizeof(buffer));
    ASSERT(strcmp("-1.024", buffer) == 0);
    double_to_string(1.0 / 3, buffer, sizeof(buffer));
    ASSERT(strcmp("0.333333333333333", buffer) == 0);
    double_to_string(2.0 / 3, buffer, sizeof(buffer));
    ASSERT(strcmp("0.666666666666667", buffer) == 0);
    return true;
}

bool test_format_string() {
    string_value_t value = format_string(L"test");
    ASSERT(wcscmp(L"test", value.data) == 0);
    ASSERT(value.length == 4);
    ASSERT(value.should_free == false);
    value = format_string(L"a%cb", L'c');
    ASSERT(wcscmp(L"acb", value.data) == 0);
    ASSERT(value.length == 3);
    ASSERT(value.should_free == true);
    FREE(value.data);
    value = format_string(L"aaa%sbbb", L"ccc");
    ASSERT(wcscmp(L"aaacccbbb", value.data) == 0);
    ASSERT(value.length == 9);
    ASSERT(value.should_free == true);
    FREE(value.data);
    value = format_string(L"value = %f%%;", (double)-1.024);
    ASSERT(wcscmp(L"value = -1.024%;", value.data) == 0);
    ASSERT(value.length == 16);
    ASSERT(value.should_free == true);
    FREE(value.data);
    value = format_string(L"value = %d;", -1);
    ASSERT(wcscmp(L"value = -1;", value.data) == 0);
    ASSERT(value.should_free == true);
    FREE(value.data);
    value = format_string(L"value = %u;", 777);
    ASSERT(wcscmp(L"value = 777;", value.data) == 0);
    ASSERT(value.should_free == true);
    FREE(value.data);
    int64_t l = 1000000000000L;
    value = format_string(L"value = %li;", l);
    ASSERT(wcscmp(L"value = 1000000000000;", value.data) == 0);
    ASSERT(value.should_free == true);
    FREE(value.data);
    size_t s = 17;
    value = format_string(L"size = %zu;", s);
    ASSERT(wcscmp(L"size = 17;", value.data) == 0);
    ASSERT(value.should_free == true);
    FREE(value.data);
    return true;
}

bool test_align_text() {
    string_value_t text = align_text((string_value_t){ NULL, 0, false }, 7, ALIGN_CENTER);
    ASSERT(text.length == 0);
    ASSERT(text.should_free == false);
    text = align_text(STATIC_STRING(L"abc"), 9, ALIGN_LEFT);
    ASSERT(text.length == 9);
    ASSERT(wcscmp(L"abc      ", text.data) == 0);
    if (text.should_free) {
        FREE(text.data);
    }
    text = align_text(STATIC_STRING(L"abc"), 11, ALIGN_CENTER);
    ASSERT(text.length == 11);
    ASSERT(wcscmp(L"    abc    ", text.data) == 0);
    if (text.should_free) {
        FREE(text.data);
    }
    text = align_text(STATIC_STRING(L"abc"), 13, ALIGN_RIGHT);
    ASSERT(text.length == 13);
    ASSERT(wcscmp(L"          abc", text.data) == 0);
    if (text.should_free) {
        FREE(text.data);
    }
    text = align_text(STATIC_STRING(L"abcdef"), 3, ALIGN_LEFT);
    ASSERT(text.length == 3);
    ASSERT(wcscmp(L"abc", text.data) == 0);
    if (text.should_free) {
        FREE(text.data);
    }
    return true;
}
