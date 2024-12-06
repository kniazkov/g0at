/**
 * @file unit_testing.c
 * @copyright 2025 Ivan Kniazkov
 * @brief A program for running unit tests.
 */

#include <stdio.h>
#include <stdlib.h>
#include "test/test_list.h"

/**
 * @brief Executes the registered unit tests and reports the results.
 *
 * This function runs all unit tests that are registered in the test list. It iterates over 
 * the list of tests, calls each test function, and counts how many tests pass or fail.
 * If any test fails, it prints the name of the failed test. The function prints the total 
 * number of tests executed and their results at the end. It returns `true` if all tests passed, 
 * and `false` if any test failed.
 *
 * @return `true` if all tests passed, `false` if any test failed.
 */
static bool unit_testing() {
    int passed = 0;
    int failed = 0;
    int count = get_number_of_tests();
    const test_description_t *tests = get_tests();
    for (int i = 0; i < count; i++) {
        bool result = tests[i].test();
        if (result) {
            passed++;
        } else {
            failed++;
            printf("Test '%s' failed\n", tests[i].name);
        }
    }

    printf("Unit testing done; total: %d, passed: %d, failed: %d", count, passed, failed);
    return failed == 0;
}

/**
 * @brief Entry point.
 * @return 0 if all tests passed, or `EXIT_FAILURE` if any test failed.
 */
int main() {
    bool success = unit_testing();
    if (!success) {
        exit(EXIT_FAILURE);
    }
    return 0;
}
