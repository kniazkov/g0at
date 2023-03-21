/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <thread>
#include <cstring>
#include "unit_testing.h"
#include "dbg_printer.h"
#include "model/object.h"
#include "model/generic_object.h"
#include "model/root_object.h"
#include "model/strings.h"
#include "model/numbers.h"
#include "model/built_in_functions.h"
#include "model/scope.h"
#include "model/expressions.h"
#include "model/exceptions.h"

namespace goat {

    bool test_empty_object() {
        return get_null_object()->to_string(nullptr) == L"null";
    }

    bool test_dynamic_string() {
        gc_data gc;
        const std::wstring str = L"test";
        object *obj = new dynamic_string(&gc, str);
        assert_equals(std::wstring, str, obj->to_string(nullptr));
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
        std::wstring result = obj->to_string_notation(nullptr);
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
        std::wstring result = obj->to_string_notation(nullptr);
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
        assert_equals(bool, false, str->is_instance_of(get_null_object()));
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
        assert_equals(bool, false, E->is_instance_of(get_null_object()));
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
        assert_equals(std::wstring, L"aaa", var->obj->to_string(nullptr));
        
        object *B = new generic_dynamic_object(&gc, A);
        static_string key_B(L"B");
        value = new dynamic_string(&gc, L"bbb");
        B->set_attribute(&key_B, value);
        value->release();
        var = B->get_attribute(&key_A);
        assert_not_null(var);
        assert_equals(std::wstring, L"aaa", var->obj->to_string(nullptr));
        var = B->get_attribute(&key_B);
        assert_not_null(var);
        assert_equals(std::wstring, L"bbb", var->obj->to_string(nullptr));
        
        object *C = new generic_dynamic_object(&gc, B);
        static_string key_C(L"C");
        value = new dynamic_string(&gc, L"ccc");
        C->set_attribute(&key_C, value);
        value->release();
        var = C->get_attribute(&key_A);
        assert_not_null(var);
        assert_equals(std::wstring, L"aaa", var->obj->to_string(nullptr));
        var = C->get_attribute(&key_B);
        assert_not_null(var);
        assert_equals(std::wstring, L"bbb", var->obj->to_string(nullptr));
        var = C->get_attribute(&key_C);
        assert_not_null(var);
        assert_equals(std::wstring, L"ccc", var->obj->to_string(nullptr));

        value = new dynamic_string(&gc, L"xxx");
        A->set_attribute(&key_A, value);
        value->release();
        var = C->get_attribute(&key_A);
        assert_not_null(var);
        assert_equals(std::wstring, L"xxx", var->obj->to_string(nullptr));        

        value = new dynamic_string(&gc, L"yyy");
        B->set_attribute(&key_A, value);
        value->release();
        var = C->get_attribute(&key_A);
        assert_not_null(var);
        assert_equals(std::wstring, L"yyy", var->obj->to_string(nullptr));        

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
        assert_equals(std::wstring, L"aaa", var->obj->to_string(nullptr));
        var = E->get_attribute(&key_B);
        assert_not_null(var);
        assert_equals(std::wstring, L"bbb", var->obj->to_string(nullptr));
        var = E->get_attribute(&key_C);
        assert_not_null(var);
        assert_equals(std::wstring, L"ccc", var->obj->to_string(nullptr));
        var = E->get_attribute(&key_D);
        assert_not_null(var);
        assert_equals(std::wstring, L"ddd", var->obj->to_string(nullptr));
        var = E->get_attribute(&key_E);
        assert_not_null(var);
        assert_equals(std::wstring, L"eee", var->obj->to_string(nullptr));

        value = new dynamic_string(&gc, L"xxx");
        A->set_attribute(&key_A, value);
        value->release();
        var = E->get_attribute(&key_A);
        assert_not_null(var);
        assert_equals(std::wstring, L"xxx", var->obj->to_string(nullptr));        

        value = new dynamic_string(&gc, L"yyy");
        C->set_attribute(&key_A, value);
        value->release();
        var = E->get_attribute(&key_A);
        assert_not_null(var);
        assert_equals(std::wstring, L"yyy", var->obj->to_string(nullptr));        

        value = new dynamic_string(&gc, L"zzz");
        B->set_attribute(&key_A, value);
        value->release();
        var = E->get_attribute(&key_A);
        assert_not_null(var);
        assert_equals(std::wstring, L"zzz", var->obj->to_string(nullptr));        

        A->release();
        B->release();
        C->release();
        D->release();
        E->release();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }

    void thread_create_and_delete_objects(gc_data *gc) {
        std::vector<object*> list;
        for (int i = 0; i < 1000; i++) {
            list.push_back(new generic_dynamic_object(gc, get_root_object()));
        }
        for (object* obj : list) {
            obj->release();
        }
    }

    bool test_multithreaded_object_creation() {
        gc_data gc;
        std::thread A(thread_create_and_delete_objects, &gc);
        std::thread B(thread_create_and_delete_objects, &gc);
        std::thread C(thread_create_and_delete_objects, &gc);
        A.join();
        B.join();
        C.join();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }

    bool test_square_root() {
        variable arg;
        arg.set_real_value(256);
        std::vector<variable> args = { arg };
        variable result;
        base_function *func = get_sqrt_instance()->to_function();
        func->exec(args, &result);
        double value = 0;
        bool result_is_real_value = result.get_real_value(&value);
        assert_equals(bool, true, result_is_real_value);
        assert_equals(double, 16, value);
        gc_data gc;
        arg.obj = create_real_number(&gc, 1024);
        args.clear();
        args.push_back(arg);
        func->exec(args, &result);
        value = 0;
        result_is_real_value = result.get_real_value(&value);
        assert_equals(bool, true, result_is_real_value);
        assert_equals(double, 32, value);
        arg.release();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }

    bool test_root_scope() {
        scope *root = get_root_scope();
        static_string name(L"String");
        variable *var = root->get_attribute(&name);
        assert_not_null(var);
        object *proto = var->obj;
        assert_equals(bool, true, name.is_instance_of(proto));
        return true;
    }

    bool test_main_scope() {
        gc_data gc;
        dbg_printer printer;
        scope *main = create_main_scope(&gc, &printer);
        scope *clone = main->clone();
        static_string name(L"Number");
        variable *var = clone->get_attribute(&name);
        assert_not_null(var);
        object *proto = var->obj;
        static_string key(L"x");
        variable value;
        value.set_real_value(7);
        assert_equals(bool, true, value.obj->is_instance_of(proto));
        clone->set_attribute(&key, value);
        var = clone->get_attribute(&key);
        assert_not_null(var);
        clone->release();
        main->release();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }

    bool test_expression_variable_expression() {
        gc_data gc;
        dbg_printer printer;
        scope *main = create_main_scope(&gc, &printer);
        static_string key(L"x");
        variable value;
        value.set_real_value(7);
        main->set_attribute(&key, value);
        base_string *name = &key;
        expression_variable *expr = new expression_variable(name);
        variable result = expr->calc(main);
        assert_equals(double, 7, result.data.double_value);
        main->release();
        expr->release();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }

    bool test_function_call_expression() {
        gc_data gc;
        dbg_printer printer;
        scope *main = create_main_scope(&gc, &printer);
        static_string key(L"sqrt");
        base_string *name = &key;
        std::vector<expression*> args;
        constant_real_number number(17.64);
        args.push_back(&number);
        function_call expr(name, args);
        variable result = expr.calc(main);
        assert_equals(double, 4.2, result.data.double_value);
        main->release();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }

    bool test_function_print() {
        gc_data gc;
        dbg_printer printer;
        scope *main = create_main_scope(&gc, &printer);
        static_string key(L"print");
        base_string *name = &key;
        std::vector<expression*> args;
        constant_real_number number(3.14);
        args.push_back(&number);
        function_call expr(name, args);
        expr.calc(main);
        assert_equals(std::wstring, L"3.14", printer.stream.str());        
        main->release();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }

    bool test_string_concatenation() {
        gc_data gc;
        dbg_printer printer;
        scope *main = create_main_scope(&gc, &printer);
        dynamic_string *str = new dynamic_string(&gc, L"it");
        object_as_expression *left = new object_as_expression(str);
        str->release();
        str = new dynamic_string(&gc, L" ");
        object_as_expression *center = new object_as_expression(str);
        str->release();
        str = new dynamic_string(&gc, L"works.");
        object_as_expression *right = new object_as_expression(str);
        str->release();
        expression *nested = new addition(left, center);
        left->release();
        center->release();
        expression *expr = new addition(nested, right);
        nested->release();
        right->release();
        variable result = expr->calc(main);
        assert_equals(std::wstring, L"it works.", result.to_string());
        result.release();
        expr->release();
        main->release();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }

    bool test_sqrt_function_with_illegal_argument() {
        gc_data gc;
        dbg_printer printer;
        scope *main = create_main_scope(&gc, &printer);
        static_string key(L"sqrt");
        base_string *name = &key;
        std::vector<expression*> args; // empty args list, should produce an exception
        function_call expr(name, args);
        bool oops = false;
        try {
            variable result = expr.calc(main);
        } 
        catch (runtime_exception &ex) {
            oops = true;
            const char *expected = "Illegal argument";
            const char *actual = ex.what();
            int compare_result = std::strcmp(expected, actual);
            assert_equals(int, 0, compare_result);
        }
        assert_equals(bool, true, oops);
        main->release();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }

    bool test_sum_two_real_numbers() {
        gc_data gc;
        dbg_printer printer;
        scope *main = create_main_scope(&gc, &printer);
        object *obj = create_real_number(&gc, 2);
        expression *left = new object_as_expression(obj);
        obj->release();
        expression *right = new constant_real_number(3);
        expression *expr = new addition(left, right);
        left->release();
        right->release();
        variable result = expr->calc(main);
        assert_equals(double, 5, result.data.double_value);
        expr->release();
        main->release();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }

    bool test_sum_two_real_numbers_with_exception() {
        gc_data gc;
        dbg_printer printer;
        scope *main = create_main_scope(&gc, &printer);
        object *obj = create_real_number(&gc, 2);
        expression *left = new object_as_expression(obj);
        obj->release();
        static_string key(L"sqrt");
        base_string *name = &key;
        std::vector<expression*> args; // empty args list, should produce an exception
        expression *right = new function_call(name, args);
        expression *expr = new addition(left, right);
        left->release();
        right->release();
        bool oops = false;
        try {
            variable result = expr->calc(main);
        }
        catch (runtime_exception &ex) {
            oops = true;
        }
        assert_equals(bool, true, oops);
        expr->release();
        main->release();
        assert_equals(unsigned int, 1, gc.get_count());
        gc.sweep();
        assert_equals(unsigned int, 0, gc.get_count());
        return true;
    }
}
