/**
 * @file test_list.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Header file for managing and retrieving unit tests.
 */

#pragma once

#include <stdbool.h>

/**
 * @struct test_description_t
 * @brief Represents a single unit test.
 *
 * This structure is used to store the metadata of each unit test in the project. Each test is 
 * represented by its name (a string) and a function pointer to the actual test implementation.
 *
 * The `name` field holds a descriptive name for the test, while the `test` function pointer 
 * points to the function that will be executed when the test is run. The `test` function should
 * return a boolean value (`true` for success, `false` for failure).
 */
typedef struct {
    const char *name;       /**< The name of the test. */
    bool (*test)();         /**< Function pointer to the test implementation. */
} test_description_t;

/**
 * @brief Retrieves the total number of unit tests.
 * @return The number of unit tests.
 */
int get_number_of_tests();

/**
 * @brief Retrieves a list of all registered tests.
 * @return A pointer to an array of `test_description_t` structures.
 */
const test_description_t *get_tests();