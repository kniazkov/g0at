/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "static_object.h"

namespace goat {
    
    const std::wstring null_notation(L"null");

    /**
     * @brief Null object, meaning "nothing"
     */
    class null_object : public static_object {
    public:
        object_type get_type() const override {
            return object_type::generic;
        }

        std::wstring to_string_notation(const variable* var) const override {
            return null_notation;
        }
    };

    static null_object instance;

    object * get_null_object() {
        return &instance;
    }
}
