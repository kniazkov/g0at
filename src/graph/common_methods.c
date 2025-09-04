/**
 * @file common_methods.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implements common methods for the Goat nodes.
 * 
 * This file provides implementations of methods that are shared across multiple 
 * node implementations in the Goat language. These methods are defined separately 
 * to ensure code reuse and maintainability.
 */

#include "common_methods.h"

string_value_t no_data(const node_t *node) {
    return NULL_STRING_VALUE;
};

size_t no_properties(const node_t *node) {
    return 0;
}

void no_property(const node_t *node, size_t index,
                 string_view_t *out_key, string_value_t *out_value) {
    *out_key = EMPTY_STRING_VIEW;
    *out_value = EMPTY_STRING_VALUE;
}

size_t no_children(const node_t *node) {
    return 0;
}

node_t* no_child(const node_t *node, size_t index) {
    return NULL;
}

const wchar_t* no_tags(const node_t *node, size_t index) {
    return NULL;
}
