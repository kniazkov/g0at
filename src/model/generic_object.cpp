/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <sstream>
#include "generic_object.h"

namespace goat {

    object_type generic_object::get_type() const {
        return object_type::generic;
    }

    std::wstring generic_object::to_string_notation() const {
        std::wstringstream stream;
        stream << L'{';
        bool flag = false;
        for (auto pair = children.begin(); pair != children.end(); pair++) {
            if (flag) {
                stream << ", ";
            }
            flag = true;
            stream << pair->first->to_string_notation() << L": "
                << pair->second.obj->to_string_notation();
        }
        stream << L'}';
        return stream.str();
    }
}
