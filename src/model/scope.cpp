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
            initialized = false;
        }

        variable * get_attribute(object *key) override {
            return get_own_attribute(key);
        }

        variable * get_own_attribute(object *key) override {
            if (!initialized) {
                init();
            }
            auto pair = attributes.find(key);
            if (pair != attributes.end()) {
                return &pair->second;
            }
            return nullptr;
        }
    
    protected:
        /**
         * @brief Initialization flag
         * 
         * We cannot initialize all objects in the constructor because not all static sub-objects
         * have been created at the time of creation of this object, and the order in which
         * they are created is unknown.
         */
        bool initialized;
        
        /**
         * @brief Initialization method
         */
        void init() {
            set_attribute_unsafe(&str_Object, get_root_object());
            set_attribute_unsafe(&str_Function, get_function_prototype());
            set_attribute_unsafe(&str_String, get_string_prototype());
            set_attribute_unsafe(&str_Number, get_number_prototype());
            set_attribute_unsafe(&str_Real, get_real_prototype());
            initialized = true;
        }
    };

    root_scope root_scope_instance;
    object * get_root_scope() {
        return &root_scope_instance;
    }
}
