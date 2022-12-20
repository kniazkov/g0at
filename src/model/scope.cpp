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
     * @brief A static object always exists in the system, so it is not counted
     *   by the garbage collector
     */
    class root_scope : public generic_static_object {
    public:
        /**
         * Constructor
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
    object * get_root_scope() {
        return &root_scope_instance;
    }
}
