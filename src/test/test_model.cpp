/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "unit_testing.h"
#include "model/object.h"
#include "model/generic_object.h"
#include "model/root_object.h"
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
        object *obj = new generic_dynamic_object(&gc, get_root_object());
        object *key = new dynamic_string(&gc, L"alpha");
        object *value = new dynamic_string(&gc, L"black");
        obj->set_attribute(key, value);
        key->release();
        value->release();
        key = new dynamic_string(&gc, L"beta");
        value = new dynamic_string(&gc, L"green");
        obj->set_attribute(key, value);
        key->release();
        value->release();
        key = new dynamic_string(&gc, L"gamma");
        value = new dynamic_string(&gc, L"blue");
        obj->set_attribute(key, value);
        key->release();
        value->release();
        key = new dynamic_string(&gc, L"alpha");
        value = new dynamic_string(&gc, L"red");
        obj->set_attribute(key, value);
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
        object *obj = new generic_dynamic_object(&gc, get_root_object());
        static_string key_1(L"alpha");
        static_string value_1(L"red");
        obj->set_attribute(&key_1, &value_1);
        static_string key_2(L"beta");
        static_string value_2(L"green");
        obj->set_attribute(&key_2, &value_2);
        static_string key_3(L"gamma");
        static_string value_3(L"blue");
        obj->set_attribute(&key_3, &value_3);
        assert_equals(unsigned int, 1, gc.get_count());
        std::wstring result = obj->to_string_notation();
        assert_equals(std::wstring, L"{\"alpha\": \"red\", \"beta\": \"green\", \"gamma\": \"blue\"}", result);
        obj->release();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }

    bool test_is_instance_of() {
        assert_equals(bool, true, get_root_object()->is_instance_of(get_root_object()));
        gc_data gc;
        object *str = new dynamic_string(&gc, L"test");
        assert_equals(bool, true, str->is_instance_of(get_root_object()));
        assert_equals(bool, true, str->is_instance_of(get_string_prototype()));
        assert_equals(bool, false, str->is_instance_of(get_empty_object()));
        str->release();
        object *A = new generic_dynamic_object(&gc, get_root_object());
        object *B = new generic_dynamic_object(&gc, A);
        object *C = new generic_dynamic_object(&gc, A);
        std::vector<object*> list = {B, C};
        object *D = new object_with_multiple_prototypes(&gc, list);
        object *E = new generic_dynamic_object(&gc, D);
        assert_equals(bool, true, B->is_instance_of(get_root_object()));
        assert_equals(bool, true, B->is_instance_of(A));
        assert_equals(bool, true, B->is_instance_of(B));
        assert_equals(bool, false, B->is_instance_of(C));
        assert_equals(bool, false, B->is_instance_of(D));
        assert_equals(bool, true, D->is_instance_of(get_root_object()));
        assert_equals(bool, true, D->is_instance_of(A));
        assert_equals(bool, true, D->is_instance_of(B));
        assert_equals(bool, true, D->is_instance_of(C));
        assert_equals(bool, true, D->is_instance_of(D));
        assert_equals(bool, false, D->is_instance_of(E));
        assert_equals(bool, true, E->is_instance_of(get_root_object()));
        assert_equals(bool, true, E->is_instance_of(A));
        assert_equals(bool, true, E->is_instance_of(B));
        assert_equals(bool, true, E->is_instance_of(C));
        assert_equals(bool, true, E->is_instance_of(D));
        assert_equals(bool, true, E->is_instance_of(E));
        assert_equals(bool, false, E->is_instance_of(get_empty_object()));
        A->release();
        B->release();
        C->release();
        D->release();
        E->release();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }

    bool test_get_attribute() {
        gc_data gc;
        variable *var;
        object *value;
        object *A = new generic_dynamic_object(&gc, get_root_object());
        static_string key_A(L"A");
        value = new dynamic_string(&gc, L"aaa");
        A->set_attribute(&key_A, value);
        value->release();
        var = A->get_attribute(&key_A);
        assert_not_null(var);
        assert_equals(std::wstring, L"aaa", var->obj->to_string());
        
        object *B = new generic_dynamic_object(&gc, A);
        static_string key_B(L"B");
        value = new dynamic_string(&gc, L"bbb");
        B->set_attribute(&key_B, value);
        value->release();
        var = B->get_attribute(&key_A);
        assert_not_null(var);
        assert_equals(std::wstring, L"aaa", var->obj->to_string());
        var = B->get_attribute(&key_B);
        assert_not_null(var);
        assert_equals(std::wstring, L"bbb", var->obj->to_string());
        
        object *C = new generic_dynamic_object(&gc, B);
        static_string key_C(L"C");
        value = new dynamic_string(&gc, L"ccc");
        C->set_attribute(&key_C, value);
        value->release();
        var = C->get_attribute(&key_A);
        assert_not_null(var);
        assert_equals(std::wstring, L"aaa", var->obj->to_string());
        var = C->get_attribute(&key_B);
        assert_not_null(var);
        assert_equals(std::wstring, L"bbb", var->obj->to_string());
        var = C->get_attribute(&key_C);
        assert_not_null(var);
        assert_equals(std::wstring, L"ccc", var->obj->to_string());

        value = new dynamic_string(&gc, L"xxx");
        A->set_attribute(&key_A, value);
        value->release();
        var = C->get_attribute(&key_A);
        assert_not_null(var);
        assert_equals(std::wstring, L"xxx", var->obj->to_string());        

        value = new dynamic_string(&gc, L"yyy");
        B->set_attribute(&key_A, value);
        value->release();
        var = C->get_attribute(&key_A);
        assert_not_null(var);
        assert_equals(std::wstring, L"yyy", var->obj->to_string());        

        A->release();
        B->release();
        C->release();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }

    bool test_get_attribute_multiple_inheritance() {
        gc_data gc;
        object *value;
        variable *var;
        object *A = new generic_dynamic_object(&gc, get_root_object());
        object *B = new generic_dynamic_object(&gc, A);
        object *C = new generic_dynamic_object(&gc, A);
        std::vector<object*> list = {B, C};
        object *D = new object_with_multiple_prototypes(&gc, list);
        object *E = new generic_dynamic_object(&gc, D);

        static_string key_A(L"A");
        value = new dynamic_string(&gc, L"aaa");
        A->set_attribute(&key_A, value);
        value->release();

        static_string key_B(L"B");
        value = new dynamic_string(&gc, L"bbb");
        B->set_attribute(&key_B, value);
        value->release();

        static_string key_C(L"C");
        value = new dynamic_string(&gc, L"ccc");
        C->set_attribute(&key_C, value);
        value->release();

        static_string key_D(L"D");
        value = new dynamic_string(&gc, L"ddd");
        D->set_attribute(&key_D, value);
        value->release();

        static_string key_E(L"E");
        value = new dynamic_string(&gc, L"eee");
        E->set_attribute(&key_E, value);
        value->release();

        var = E->get_attribute(&key_A);
        assert_not_null(var);
        assert_equals(std::wstring, L"aaa", var->obj->to_string());
        var = E->get_attribute(&key_B);
        assert_not_null(var);
        assert_equals(std::wstring, L"bbb", var->obj->to_string());
        var = E->get_attribute(&key_C);
        assert_not_null(var);
        assert_equals(std::wstring, L"ccc", var->obj->to_string());
        var = E->get_attribute(&key_D);
        assert_not_null(var);
        assert_equals(std::wstring, L"ddd", var->obj->to_string());
        var = E->get_attribute(&key_E);
        assert_not_null(var);
        assert_equals(std::wstring, L"eee", var->obj->to_string());

        value = new dynamic_string(&gc, L"xxx");
        A->set_attribute(&key_A, value);
        value->release();
        var = E->get_attribute(&key_A);
        assert_not_null(var);
        assert_equals(std::wstring, L"xxx", var->obj->to_string());        

        value = new dynamic_string(&gc, L"yyy");
        C->set_attribute(&key_A, value);
        value->release();
        var = E->get_attribute(&key_A);
        assert_not_null(var);
        assert_equals(std::wstring, L"yyy", var->obj->to_string());        

        value = new dynamic_string(&gc, L"zzz");
        B->set_attribute(&key_A, value);
        value->release();
        var = E->get_attribute(&key_A);
        assert_not_null(var);
        assert_equals(std::wstring, L"zzz", var->obj->to_string());        

        A->release();
        B->release();
        C->release();
        D->release();
        E->release();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }
}
