/**
 * @file unit_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief A program for running unit tests.
 */

#include <stdio.h>
#include <stdlib.h>
#include "test/test_list.h"
#include "lib/allocate.h"

/**
 * @brief Executes the registered unit tests and reports the results.
 * @return `true` if all tests passed, `false` if any test failed.
 */
static bool unit_testing() {
    int passed = 0;
    int failed = 0;
    int count = get_number_of_tests();
    const test_description_t *tests = get_tests();
    for (int i = 0; i < count; i++) {
        size_t memory_size = get_allocated_memory_size();
        bool result = tests[i].test();
        if (result) {
            size_t diff = get_allocated_memory_size() - memory_size;
            if (diff > 0) {
                failed++;
                printf("Test '%s' finished with a memory leak (%zu bytes)\n", tests[i].name, diff);
            } else {
                passed++;
            }
        } else {
            failed++;
            printf("Test '%s' failed\n", tests[i].name);
        }
    }

    printf("Unit testing done; total: %d, passed: %d, failed: %d\n", count, passed, failed);
    return failed == 0;
}

/** @brief Entry point. */
int main() {
    bool success = unit_testing();
    if (!success) {
        exit(EXIT_FAILURE);
    }
    return 0;
}
