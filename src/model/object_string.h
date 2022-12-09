/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "object.h"

namespace goat {
    
    /**
     * @brief Object that stores an unicode string
     */
    class object_string : public object {
    private:
        std::wstring value;

    public:
        object_string(std::wstring &value) {
            this->value = value;
        }

        std::wstring to_string() override {
            return value;
        }
    };
}
