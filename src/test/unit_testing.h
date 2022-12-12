/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <string>
#include <sstream>

namespace goat {

    /**
     * @brief An exception that occurred during testing if test failed
     */
    struct test_failed_exception {
        /**
         * @brief Name of the file where the exception was triggered
         */
        const char * file;

        /**
         * @brief Number of the line where the exception was triggered
         */
        int line;
        
        /**
         * @brief The message that will be displayed in the log
         */
        std::string message;

        /**
         * Constructor
         * @param _file Name of the file where the exception was triggered
         * @param _line Number of the line where the exception was triggered
         */
        test_failed_exception(const char *_file, int _line) :
                file(_file), line(_line) {
        }
    };

    template <typename T> struct assertion_equals_exception : public test_failed_exception {
        /**
         * Constructor
         * @param file Name of the file where the exception was triggered
         * @param line Number of the line where the exception was triggered
         * @param expected Expected value
         * @param actual Actual value
         */
        assertion_equals_exception(const char *file, int line, T expected, T actual) :
                test_failed_exception(file, line) {
            std::stringstream stream;
            stream << "expected '" << expected << "' but actual '" << actual << "'.";
            message = stream.str();
        }
    };

    #define assert_equals(T, expected, actual) if ((expected) != (actual)) throw assertion_equals_exception<T>(__FILE__, __LINE__, expected, actual)
}
