/**
 * @file test_list.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Header file for managing and retrieving unit tests.
 */

#pragma once

#include <stdbool.h>

/** @brief A single unit test. */
typedef struct {
    const char *name;       /**< The name of the test. */
    bool (*test)();         /**< Function pointer to the test implementation. */
} test_description_t;

/** @brief Retrieves the total number of unit tests. */
int get_number_of_tests();

/** @brief Retrieves a list of all registered tests. */
const test_description_t *get_tests();