/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "data_type.h"

namespace goat {
    /**
     * @brief Integer data type descriptor
     */
    class integer_data_type : public data_type {
        const char * get_cpp_type() const override {
            return "int64_t";
        }
    };

    static integer_data_type integer_type_instance;
    data_type * get_integer_data_type() {
        return &integer_type_instance;
    }
}
