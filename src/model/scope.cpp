/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "scope.h"
#include "generic_object.h"
#include "root_object.h"
#include "functions.h"
#include "strings.h"
#include "numbers.h"
#include "exceptions.h"

namespace goat {

    static_string str_Object(L"Object");
    static_string str_Function(L"Function");
    static_string str_String(L"String");
    static_string str_Number(L"Number");
    static_string str_Real(L"Real");
    static_string str_Exception(L"Exception");
    static_string str_IllegalArgument(L"IllegalArgument");
    static_string str_OperationNotSupported(L"OperationNotSupported");

    static_string str_print(L"print");
    static_string str_sqrt(L"sqrt");

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
            write_static_attribute(&str_Exception, get_exception_prototype());
            write_static_attribute(&str_IllegalArgument, get_illegal_agrument_exception());
            write_static_attribute(&str_OperationNotSupported,
                get_operation_not_supported_exception());

            write_static_attribute(&str_sqrt, get_sqrt_instance());
        }

        scope * clone() override {
            return this;
        }

        scope * clone(object *proto) override {
            return this;
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
        /**
         * @brief Constructor
         * @param gc Data required for the garbage collector
         * @param proto The prototype scope
         */
        scope_with_one_prototype(gc_data *gc, scope *proto) : generic_dynamic_object(gc, proto) {
        }

        scope * clone() override;
        scope * clone(object *proto) override;
    };

    /**
     * @brief Scope with two prototypes
     */
    class scope_with_two_prototypes : public object_with_multiple_prototypes, public scope {
    public:
        /**
         * @brief Constructor
         * @param gc Data required for the garbage collector
         * @param first The first prototype
         * @param second The second prototype
         */
        scope_with_two_prototypes(gc_data *gc, object *first, scope *second) 
                : object_with_multiple_prototypes(gc, std::vector<object*>{first, second}) {
        }

        scope * clone() override;
        scope * clone(object *proto) override;
    };

    scope * scope_with_one_prototype::clone() {
        return new scope_with_one_prototype(get_garbage_collector_data(), this);
    }

    scope * scope_with_one_prototype::clone(object *proto) {
        return new scope_with_two_prototypes(get_garbage_collector_data(), proto, this);
    }

    scope * scope_with_two_prototypes::clone() {
        return new scope_with_one_prototype(get_garbage_collector_data(), this);
    }

    scope * scope_with_two_prototypes::clone(object *proto) {
        return new scope_with_two_prototypes(get_garbage_collector_data(), proto, this);
    }

    /**
     * @brief The main scope, that is, the one in which code execution begins
     */
    class main_scope : public scope_with_one_prototype {
    public:
        /**
         * @brief Constructor
         * @param gc Data required for the garbage collector
         * @param printer An abstract printer that actually does the printing
         */
        main_scope(gc_data *gc, function_print::printer *printer) :
                scope_with_one_prototype(gc, &root_scope_instance) {
            function_print *fn = new function_print(gc, printer);
            variable var;
            var.obj = fn;
            set_attribute(&str_print, var);
            fn->release();
        }
    };

    scope * create_main_scope(gc_data *gc, function_print::printer *printer) {
        return new main_scope(gc, printer);
    }
}
