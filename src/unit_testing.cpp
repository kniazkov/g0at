/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <iostream>
#include <cstdlib>
#include "test/unit_testing.h"
#include "test/test_list.h"

namespace goat {

    bool unit_testing() {
        unsigned int passed = 0;
        unsigned int failed = 0;
        unsigned int count = get_number_of_tests();
        const test_description *tests = get_tests();
        for (unsigned int i = 0; i < count; i++) {
            bool result = false;
            try {
                result = tests[i].test();
            } catch (test_failed_exception ex) {
                std::wcout << "Test '" << tests[i].name << "': " 
                    << ex.file << ", " << ex.line << ": " << ex.message << std::endl;
            }
            if (result) {
                passed++;
            } else {
                failed++;
                std::cout << "Test '" << tests[i].name << "' failed." << std::endl;
            }
        }
        std::cout << "Unit testing done; total: " << count 
            << ", passed: " << passed << ", failed: " << failed << '.' << std::endl;
        return failed == 0;
    }
}

int main() {
    bool success = goat::unit_testing();
    if (!success)
        exit(EXIT_FAILURE);
    return 0;
}
