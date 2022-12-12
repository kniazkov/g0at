/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include <sstream>
#include "strings.h"

namespace goat {

    object_type dynamic_string::get_type() const {
        return object_type::string;
    }

    bool dynamic_string::less(const object* const other) const {
        std::wstring other_value;
        bool other_is_a_string = other->get_string_value(&other_value);
        assert(other_is_a_string);
        return value < other_value;
    }

    std::wstring dynamic_string::to_string() const {
        return value;
    }

    /**
     * @todo Escape entities
     */
    std::wstring dynamic_string::to_string_notation() const {
        std::wstringstream wss;
        wss << L'"' << value << L'"';
        return wss.str();
    }

    bool dynamic_string::get_string_value(std::wstring* const value_ptr) const {
        *value_ptr = value;
        return true;
    }
}
