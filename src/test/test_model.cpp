/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "unit_testing.h"
#include "object.h"
#include "generic_object.h"
#include "strings.h"

namespace goat {

    bool test_empty_object() {
        return get_empty_object()->to_string() == L"{}";
    }

    bool test_dynamic_string() {
        gc_data gc;
        const std::wstring str = L"test";
        object *obj = new dynamic_string(&gc, str);
        assert_equals(std::wstring, str, obj->to_string());
        assert_equals(unsigned int, 1, gc.get_count());
        obj->release();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }

    bool test_generic_object_string_notation() {
        gc_data gc;
        object *obj = new generic_object(&gc);
        object *key = new dynamic_string(&gc, L"alpha");
        object *value = new dynamic_string(&gc, L"black");
        obj->set_child(key, value);
        key->release();
        value->release();
        key = new dynamic_string(&gc, L"beta");
        value = new dynamic_string(&gc, L"green");
        obj->set_child(key, value);
        key->release();
        value->release();
        key = new dynamic_string(&gc, L"gamma");
        value = new dynamic_string(&gc, L"blue");
        obj->set_child(key, value);
        key->release();
        value->release();
        key = new dynamic_string(&gc, L"alpha");
        value = new dynamic_string(&gc, L"red");
        obj->set_child(key, value);
        key->release();
        value->release();
        std::wstring result = obj->to_string_notation();
        obj->release();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }
}
