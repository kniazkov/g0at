/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include "expressions.h"
#include "functions.h"
#include "exceptions.h"

namespace goat {

    expression_object::expression_object(object *obj) {
        this->obj = obj;
        obj->add_reference();
    }

    expression_object::~expression_object() {
        obj->release();
    }

    variable expression_object::calc(scope *scope) {
        obj->add_reference();
        variable var;
        var.obj = obj;
        return var;
    }

    /* ----------------------------------------------------------------------------------------- */

    constant_real_number::constant_real_number(double value) : value(value) {
    }

    variable constant_real_number::calc(scope *scope) {
        variable var;
        var.set_real_value(value);
        return var;
    }

    /* ----------------------------------------------------------------------------------------- */

    read_variable::read_variable(base_string *name) {
        this->name = name;
        name->add_reference();
    }

    read_variable::~read_variable() {
        name->release();
    }

    /**
     * @todo Exception if variable not found
     */
    variable read_variable::calc(scope *scope) {
        return *(scope->get_attribute(name));
    }

    /* ----------------------------------------------------------------------------------------- */

    function_call::function_call(base_string *name, std::vector<expression*> &args) {
        this->name = name;
        this->args = args;
 
        name->add_reference();
        for (auto arg : args) {
            arg->add_reference();
        }
    }

    function_call::~function_call() {
        name->release();
        for (auto arg : args) {
            arg->release();
        }
    }

    /**
     * @todo Exception if function not found
     */
    variable function_call::calc(scope *scope) {
        variable *var = scope->get_attribute(name);
        assert(var != nullptr);
        base_function *func = var->to_function();
        assert(func != nullptr);
        variable ret_val;
        std::vector<variable> var_list;
        for (auto arg : args) {
            var_list.push_back(arg->calc(scope));
        }
        func->exec(var_list, &ret_val);        
        return ret_val;
    }

    /* ----------------------------------------------------------------------------------------- */

    binary_operation::binary_operation(expression *left, expression *right) {
        this->left = left;
        left->add_reference();
        this->right = right;
        right->add_reference();
    }

    binary_operation::~binary_operation() {
        left->release();
        right->release();
    }

    variable binary_operation::calc(scope *scope) {
        variable left_value,
            right_value,
            result;
        
        try {
            left_value = left->calc(scope);
            right_value = right->calc(scope);
            result = calc(scope, &left_value, &right_value);
        }
        catch(goat_exception_wrapper ex) {
            left_value.release();
            right_value.release();
            throw;
        }

        left_value.release();
        right_value.release();
        return result;
    }

    variable addition::calc(scope *scope, variable *left, variable *right) {
        return left->obj->do_addition(scope->get_garbage_collector_data(), left, right);
    }
}
