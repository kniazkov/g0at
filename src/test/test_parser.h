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

/**
 * @brief Tests the case when the bracket is not closed.
 * @return True if the test passes, false otherwise.
 */
bool test_unclosed_bracket();

/**
 * @brief Tests the case when an opening bracket is missing.
 * @return True if the test passes, false otherwise.
 */
bool test_missing_opening_bracket();

/**
 * @brief Tests the case when the closing bracket does not match the opening bracket.
 * @return True if the test passes, false otherwise.
 */
bool test_closing_bracket_does_not_match_opening();
