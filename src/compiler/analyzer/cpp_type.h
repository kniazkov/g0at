/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

namespace goat {

    /**
     * @brief A native C++ data type to which Goat expressions can be cast
     * 
     * It is used when generating C++ code.
     */
    enum class cpp_type : int {
        /**
         * @brief Unknown data type - for variables that have not been assigned
         */
        unknown = 0,

        /**
         * @brief Invalid data type, this code cannot be converted to C++
         */
        invalid,

        /**
         * @brief Type can be converted to <code>std::wstring</code>
         */
        string,

        /**
         * @brief Type can be converted to <code>int64_t</code>
         */
        integer,

        /**
         * @brief Type can be converted to <code>double</code>
         */
        real
    };

    /**
     * @brief Converts C++ type to string
     * @param cpp_type C++ type
     * @return String representation of C++ type
     */
    const char * cpp_type_to_string(cpp_type type);
}
