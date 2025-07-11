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

/**
 * @brief Tests object methods `set_property` and `get_property`.
 * @return `true` if the test passes, `false` if it fails.
 */
bool test_properties();

/**
 * @brief Gets the topology of the string object and the properties of its first prototype.
 * @return `true` if the test passes, `false` if it fails.
 */
bool test_string_topology();

/**
 * @brief Tests the `STORE` and `VLOAD` opcodes by verifying data persistence and retrieval.
 * @return `true` if the test passes, `false` if it fails.
 */
bool test_store_and_load();

/**
 * @brief Tests the `CALL` opcode with the built-in `sign` function.
 * @return Returns `true` if the test passes, or `false` if it fails.
 */
bool test_sign_function();

/**
 * @brief Tests `ENTER` and `LEAVE` opcodes with execution of some code inside a new context.
 * @return Returns `true` if the test passes, or `false` if it fails.
 */
bool test_context_cloning();
