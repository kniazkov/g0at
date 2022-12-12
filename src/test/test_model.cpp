/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "object.h"
#include "strings.h"
#include "unit_testing.h"

namespace goat {

    bool test_empty_object() {
        return get_empty_object()->to_string() == L"{}";
    }

    bool test_dynamic_string() {
        bool result;
        gc_data gc;
        const std::wstring str = L"test";
        object *obj = new dynamic_string(&gc, str);
        result = str == obj->to_string();
        assert_equals(unsigned int, 1, gc.get_count());
        obj->release();
        assert_equals(unsigned int, 0, gc.get_count());
        return result;
    }
}
