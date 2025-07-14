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
    return (string_value_t) { NULL, 0, false };
};


size_t no_children(const node_t *node) {
    return 0;
}

const node_t* no_child(const node_t *node, size_t index) {
    return NULL;
}

const wchar_t* no_tags(const node_t *node, size_t index) {
    return NULL;
}
