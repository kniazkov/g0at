/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "object.h"
#include "object_string.h"

namespace goat {

    bool test_empty_object() {
        return get_empty_object()->to_string() == L"{}";
    }

    bool test_object_string() {
        std::wstring str = L"test";
        object *obj = new object_string(str);
        return obj->to_string() == str;
    }
}
