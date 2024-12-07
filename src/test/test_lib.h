/**
 * @file test_lib.c
 * @copyright 2025 Ivan Kniazkov
 * @brief A set of tests for testing extensions to the С standard library
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Tests memory allocation, initialization, and deallocation.
 * @return `true` if all memory operations (allocation, initialization, deallocation)
 *         pass successfully, `false` if any of the tests fail.
 */
bool test_memory_allocation();

bool test_avl_tree();