/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

namespace goat {

    /**
     * @brief A Goat data type descriptor
     */
    class data_type {
        /**
         * @brief Returns a native C++ data type to which Goat expressions can be cast
         * @return Goat string representing C++ type
         */
        virtual const char * get_cpp_type() const = 0;
    };

    /**
     * @brief Returns pointer to integer type descriptor
     */
    data_type * get_integer_data_type();
}
