/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "static_object.h"

namespace goat {
    
    /**
     * @brief Empty object that contains no other objects
     */
    class empty_object : public static_object {

        object_type get_type() const override {
            return object_type::generic;
        }

        std::wstring to_string_notation(const variable* var) const override {
            return L"{}";
        }
    };

    static empty_object instance;

    object * get_empty_object() {
        return &instance;
    }
}
