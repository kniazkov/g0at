/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <iostream>
#include "test_model.h"

namespace goat {

    struct test_description {
        const char *name;
        bool (*test)();
    };

    static test_description test_list[] = {
        { "empty object", test_empty_object },
        { "string object", test_object_string }
    };

    int unit_testing() {
        int passed = 0;
        int failed = 0;
        int count = sizeof(test_list) / sizeof(test_description);
        for (int i = 0; i < count; i++) {
            bool result = test_list[i].test();
            if (result) {
                passed++;
            } else {
                failed++;
                std::cout << "Test '" << test_list[i].name << "' failed." << std::endl;
            }
        }
        std::cout << "Unit testing done; total: " << count 
            << ", passed: " << passed << ", failed: " << failed << '.' << std::endl;
        return failed != 0 ? -1 : 0;
    }   
}

int main(int argc, char** argv) {
    return goat::unit_testing();
}
