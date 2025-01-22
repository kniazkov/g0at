/**
 * @file test_lib.h
 * @copyright 2025 Ivan Kniazkov
 * @brief A set of tests for testing extensions to the С standard library.
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Tests memory allocation, initialization, and deallocation.
 * @return `true` if all memory operations (allocation, initialization, deallocation)
 *  pass successfully, `false` if any of the tests fail.
 */
bool test_memory_allocation();

/**
 * @brief Unit test for AVL tree implementation.
 * @return `true` if the test passes, `false` if any assertion fails.
 */
bool test_avl_tree();

/**
 * @brief Unit test covering the string builder.
 * @return `true` if the test passes, `false` if any assertion fails.
 */
bool test_string_builder();

/**
 * @brief Unit test covering the binary search algorithm.
 * @return `true` if the test passes, `false` if any assertion fails.
 */
bool test_binary_search();

/**
 * @brief Unit test covering real value to string transformation.
 * @return `true` if the test passes, `false` if any assertion fails.
 */
bool test_double_to_string();

/**
 * @brief Unit test covering string formatting.
 * @return `true` if the test passes, `false` if any assertion fails.
 */
bool test_format_string();
