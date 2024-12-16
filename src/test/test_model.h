/**
 * @file test_model.h
 * @copyright 2025 Ivan Kniazkov
 * @brief A set of tests for testing object model.
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Tests the behavior of the boolean object in the Goat language.
 * @return True if the test passes, false otherwise.
 */
bool test_boolean_object();

/**
 * @brief Tests the behavior of the integer object in the Goat language.
 * @return True if the test passes, false otherwise.
 */
bool test_integer_object();

/**
 * @brief Tests addition of two integers in the virtual machine.
 * @return `true` if the test passes, `false` if it fails.
 */
bool test_addition_of_two_integers();

/**
 * @brief Tests subtraction of two integers in the virtual machine.
 * @return `true` if the test passes, `false` if it fails.
 */
bool test_subtraction_of_two_integers();

/**
 * @brief Tests the concatenation of three strings.
 * @return `true` if the test passes, `false` if it fails.
 */
bool test_strings_concatenation();
