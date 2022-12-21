/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "scope.h"
#include "generic_object.h"
#include "root_object.h"
#include "functions.h"
#include "strings.h"
#include "numbers.h"

namespace goat {

    static_string str_Object(L"Object");
    static_string str_Function(L"Function");
    static_string str_String(L"String");
    static_string str_Number(L"Number");
    static_string str_Real(L"Real");

    /**
     * @brief Root scope, i.e. set of built-in objects
     */
    class root_scope : public generic_static_object, public scope {
    public:
        /**
         * @brief Constructor
         */
        root_scope() {
            write_static_attribute(&str_Object, get_root_object());
            write_static_attribute(&str_Function, get_function_prototype());
            write_static_attribute(&str_String, get_string_prototype());
            write_static_attribute(&str_Number, get_number_prototype());
            write_static_attribute(&str_Real, get_real_prototype());
        }
    };

    root_scope root_scope_instance;
    scope * get_root_scope() {
        return &root_scope_instance;
    }

    /**
     * @brief Scope with one prototype
     */
    class scope_with_one_prototype : public generic_dynamic_object, public scope {
    public:
        scope_with_one_prototype(gc_data *gc, scope *proto) : generic_dynamic_object(gc, proto) {
        } 
    };

    scope * create_main_scope(gc_data *gc) {
        return new scope_with_one_prototype(gc, &root_scope_instance);
    }
}
