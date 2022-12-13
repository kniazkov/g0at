/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <iostream>
#include <cstdlib>
#include "test/unit_testing.h"
#include "test/test_model.h"

namespace goat {

    struct test_description {
        const char *name;
        bool (*test)();
    };

    static test_description test_list[] = {
        { "empty object", test_empty_object },
        { "dynamic string", test_dynamic_string },
        { "string notation of a generic object", test_generic_object_string_notation },
        { "static strings declaration", test_static_string_declaration }
    };

    bool unit_testing() {
        int passed = 0;
        int failed = 0;
        int count = sizeof(test_list) / sizeof(test_description);
        for (int i = 0; i < count; i++) {
            bool result = false;
            try {
                result = test_list[i].test();
            } catch (test_failed_exception ex) {
                std::wcout << "Test '" << test_list[i].name << "': " 
                    << ex.file << ", " << ex.line << ": " << ex.message << std::endl;
            }
            if (result) {
                passed++;
            } else {
                failed++;
                std::cout << "Test '" << test_list[i].name << "' failed." << std::endl;
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
