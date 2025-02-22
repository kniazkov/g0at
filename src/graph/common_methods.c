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

bool stub_indented_goat_code_generator(const node_t *node, source_builder_t *builder,
        size_t indent) {
    return false;
}
