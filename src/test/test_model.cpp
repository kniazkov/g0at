/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "unit_testing.h"
#include "model/object.h"
#include "model/generic_object.h"
#include "model/strings.h"

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
        assert_equals(unsigned int, 7, gc.get_count());
        std::wstring result = obj->to_string_notation();
        assert_equals(std::wstring, L"{\"alpha\": \"red\", \"beta\": \"green\", \"gamma\": \"blue\"}", result);
        obj->release();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }

    bool test_static_string_declaration() {
        gc_data gc;
        object *obj = new generic_object(&gc);
        static_string key_1(L"alpha");
        static_string value_1(L"red");
        obj->set_child(&key_1, &value_1);
        static_string key_2(L"beta");
        static_string value_2(L"green");
        obj->set_child(&key_2, &value_2);
        static_string key_3(L"gamma");
        static_string value_3(L"blue");
        obj->set_child(&key_3, &value_3);
        assert_equals(unsigned int, 1, gc.get_count());
        std::wstring result = obj->to_string_notation();
        assert_equals(std::wstring, L"{\"alpha\": \"red\", \"beta\": \"green\", \"gamma\": \"blue\"}", result);
        obj->release();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }
}


