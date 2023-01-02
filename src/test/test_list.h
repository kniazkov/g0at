/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

namespace goat {

    /**
     * @brief Test description
     */
    struct test_description {
        /**
         * @brief The name of the test
         */
        const char *name;

        /**
         * @brief Testing method
         */
        bool (*test)();
    };

    /**
     * @return The number of all tests 
     */
    unsigned int get_number_of_tests();

    /**
     * @return The table containing all the tests
     */
    const test_description * get_tests();
}
