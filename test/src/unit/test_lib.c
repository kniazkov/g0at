/**
 * @file test_lib.c
 * @copyright 2026 Ivan Kniazkov
 * @brief A set of tests for testing extensions to the С standard library.
 */

#include "test_lib.h"

#include "goat/unicode.h"
#include "lib/collections/avl_tree.h"
#include "lib/collections/pair.h"
#include "lib/collections/vector.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "test_macro.h"

#include <stdint.h>
#include <stdio.h>

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

/** @brief Callback function for iterating over the AVL tree. */
static void avl_callback(void *user_data, void *key, value_t value) {
    vector_t *keys = (vector_t *)user_data;
    append_to_vector(keys, WSTRDUP((gchar_t *)key));
}

bool test_avl_tree() {
    avl_tree_t *tree = create_avl_tree(string_comparator);
    set_in_avl_tree(tree, U"gamma", (value_t){.ptr = U"third"});
    value_t previous = set_in_avl_tree(tree, U"alpha", (value_t){.ptr = U"first"});
    ASSERT(previous.ptr == NULL);
    set_in_avl_tree(tree, U"beta", (value_t){.ptr = U"second"});
    ASSERT(avl_tree_contains(tree, U"alpha"));
    ASSERT(false == avl_tree_contains(tree, U"delta"));
    vector_t *keys = create_vector();
    avl_tree_for_each(tree, avl_callback, keys);
    ASSERT(keys->size == 3);
    ASSERT(STRCMP(U"alpha", (gchar_t *)keys->data[0]) == 0);
    ASSERT(STRCMP(U"beta", (gchar_t *)keys->data[1]) == 0);
    ASSERT(STRCMP(U"gamma", (gchar_t *)keys->data[2]) == 0);
    destroy_vector_ex(keys, FREE);
    ASSERT(STRCMP(U"first", (gchar_t *)get_from_avl_tree(tree, U"alpha").ptr) == 0);
    ASSERT(STRCMP(U"second", (gchar_t *)get_from_avl_tree(tree, U"beta").ptr) == 0);
    ASSERT(STRCMP(U"third", (gchar_t *)get_from_avl_tree(tree, U"gamma").ptr) == 0);
    ASSERT(get_from_avl_tree(tree, U"delta").ptr == NULL);
    previous = set_in_avl_tree(tree, U"alpha", (value_t){.ptr = U"primary"});
    ASSERT(STRCMP(U"first", previous.ptr) == 0);
    ASSERT(STRCMP(U"primary", (gchar_t *)get_from_avl_tree(tree, U"alpha").ptr) == 0);
    destroy_avl_tree(tree);
    return true;
}

bool test_string_builder() {
    string_builder_t sb;
    init_string_builder(&sb, 0);
    string_value_t result = append_string(&sb, U"it");
    ASSERT(STRCMP(U"it", result.data) == 0);
    ASSERT(result.length == 2);
    result = append_char(&sb, U' ');
    ASSERT(STRCMP(U"it ", result.data) == 0);
    ASSERT(result.length == 3);
    result = append_string(&sb, U"works");
    ASSERT(STRCMP(U"it works", result.data) == 0);
    ASSERT(result.length == 8);
    FREE_STRING(result);
    return true;
}

bool test_binary_search() {
    pair_t pairs[] = {{U"fifth", U"five"},
                      {U"first", U"one"},
                      {U"fourth", U"four"},
                      {U"second", U"two"},
                      {U"third", U"three"}};
    ASSERT(STRCMP(U"one", (gchar_t *)binary_search(pairs, 5, U"first", string_comparator)) == 0);
    ASSERT(STRCMP(U"two", (gchar_t *)binary_search(pairs, 5, U"second", string_comparator)) == 0);
    ASSERT(STRCMP(U"three", (gchar_t *)binary_search(pairs, 5, U"third", string_comparator)) == 0);
    ASSERT(STRCMP(U"four", (gchar_t *)binary_search(pairs, 5, U"fourth", string_comparator)) == 0);
    ASSERT(STRCMP(U"five", (gchar_t *)binary_search(pairs, 5, U"fifth", string_comparator)) == 0);
    ASSERT(binary_search(pairs, 5, U"sixth", string_comparator) == NULL);
    return true;
}

bool test_double_to_string() {
    char buffer[32];
    double_to_string(1.0, buffer, sizeof(buffer));
    ASSERT(strcmp("1.0", buffer) == 0);
    double_to_string(-1.024, buffer, sizeof(buffer));
    ASSERT(strcmp("-1.024", buffer) == 0);
    double_to_string(1.0 / 3, buffer, sizeof(buffer));
    ASSERT(strcmp("0.33333333333", buffer) == 0);
    double_to_string(2.0 / 3, buffer, sizeof(buffer));
    ASSERT(strcmp("0.66666666667", buffer) == 0);
    return true;
}

bool test_format_string() {
    string_value_t value = format_string(U"test");
    ASSERT(STRCMP(U"test", value.data) == 0);
    ASSERT(value.length == 4);
    ASSERT(value.should_free == false);
    value = format_string(U"a%cb", U'c');
    ASSERT(STRCMP(U"acb", value.data) == 0);
    ASSERT(value.length == 3);
    ASSERT(value.should_free == true);
    FREE_STRING(value);
    value = format_string(U"aaa%sbbb", U"ccc");
    ASSERT(STRCMP(U"aaacccbbb", value.data) == 0);
    ASSERT(value.length == 9);
    ASSERT(value.should_free == true);
    FREE_STRING(value);
    value = format_string(U"value = %f%%;", (double)-1.024);
    ASSERT(STRCMP(U"value = -1.024%;", value.data) == 0);
    ASSERT(value.length == 16);
    ASSERT(value.should_free == true);
    FREE_STRING(value);
    value = format_string(U"value = %d;", -1);
    ASSERT(STRCMP(U"value = -1;", value.data) == 0);
    ASSERT(value.should_free == true);
    FREE_STRING(value);
    value = format_string(U"value = %u;", 777);
    ASSERT(STRCMP(U"value = 777;", value.data) == 0);
    ASSERT(value.should_free == true);
    FREE_STRING(value);
    int64_t l = 1000000000000L;
    value = format_string(U"value = %li;", l);
    ASSERT(STRCMP(U"value = 1000000000000;", value.data) == 0);
    ASSERT(value.should_free == true);
    FREE_STRING(value);
    size_t s = 17;
    value = format_string(U"size = %zu;", s);
    ASSERT(STRCMP(U"size = 17;", value.data) == 0);
    ASSERT(value.should_free == true);
    FREE_STRING(value);
    return true;
}

bool test_align_text() {
    string_value_t text = align_text(NULL_STRING_VALUE, 7, ALIGN_CENTER);
    ASSERT(text.length == 0);
    ASSERT(text.should_free == false);
    text = align_text(STATIC_STRING(U"abc"), 9, ALIGN_LEFT);
    ASSERT(text.length == 9);
    ASSERT(STRCMP(U"abc      ", text.data) == 0);
    FREE_STRING(text);
    text = align_text(STATIC_STRING(U"abc"), 11, ALIGN_CENTER);
    ASSERT(text.length == 11);
    ASSERT(STRCMP(U"    abc    ", text.data) == 0);
    FREE_STRING(text);
    text = align_text(STATIC_STRING(U"abc"), 13, ALIGN_RIGHT);
    ASSERT(text.length == 13);
    ASSERT(STRCMP(U"          abc", text.data) == 0);
    FREE_STRING(text);
    text = align_text(STATIC_STRING(U"abcdef"), 3, ALIGN_LEFT);
    ASSERT(text.length == 3);
    ASSERT(STRCMP(U"abc", text.data) == 0);
    FREE_STRING(text);
    return true;
}
