/**
 * @file test_scanner.h
 * @copyright 2025 Ivan Kniazkov
 * @brief A set of tests for testing scanner.
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Tests the scanner with an identifier token.
 * @return True if the test passes, false otherwise.
 */
bool test_identifier();

/**
 * @brief Tests the scanner with a bracket token.
 * @return True if the test passes, false otherwise.
 */
bool test_bracket();

/**
 * @brief Tests the scanner with a string token.
 * @return True if the test passes, false otherwise.
 */
bool test_static_string();

/**
 * @brief Tests the scanner with invalid symbol.
 * @return True if the test passes, false otherwise.
 */
bool test_uknown_symbol();
