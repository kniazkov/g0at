/*
    Copyright 2025 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <stdbool.h>

/**
 * @brief Test description
 */
typedef struct {
    /**
     * @brief The name of the test
     */
    const char *name;

    /**
     * @brief Testing method
     */
    bool (*test)();
}  test_description_t;

/**
 * @return The number of all tests 
 */
int get_number_of_tests();

/**
 * @return The table containing all the tests
 */
const test_description_t *get_tests();
