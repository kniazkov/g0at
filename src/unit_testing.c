/*
    Copyright 2025 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <stdio.h>
#include <stdlib.h>
#include "test/test_list.h"

bool unit_testing() {
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

int main() {
    bool success = unit_testing();
    if (!success) {
        exit(EXIT_FAILURE);
    }
    return 0;
}
