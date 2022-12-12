/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <sstream>
#include "object_string.h"

namespace goat {

    object_type object_string::get_type() const {
        return object_type::string;
    }

    bool object_string::less(const object *other) const {
        object_string *str = (object_string*)other;
        return value < str->value;
    }

    std::wstring object_string::to_string() const {
        return value;
    }

    /**
     * @todo Escape entities
     */
    std::wstring object_string::to_string_notation() const {
        std::wstringstream wss;
        wss << L'"' << value << L'"';
        return wss.str();
    }
}
