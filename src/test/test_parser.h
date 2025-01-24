/**
 * @file test_scanner.h
 * @copyright 2025 Ivan Kniazkov
 * @brief A set of tests for testing parser.
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Tests parsing of brackets with one level of nesting.
 * @return True if the test passes, false otherwise.
 */
bool test_brackets_one_level_nesting();

/**
 * @brief Tests parsing of brackets with two levels of nesting.
 * @return True if the test passes, false otherwise.
 */
bool test_brackets_two_levels_nesting();
