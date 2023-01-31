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

    std::vector<child_descriptor> expression_object::get_children() const {
        return {};
    }

    variable expression_object::calc(scope *scope) {
        obj->add_reference();
        variable var;
        var.obj = obj;
        return var;
    }

    /* ----------------------------------------------------------------------------------------- */

    constant_integer_number::constant_integer_number(int64_t value) : value(value) {
    }

    std::vector<child_descriptor> constant_integer_number::get_children() const {
        return {};
    }

    variable constant_integer_number::calc(scope *scope) {
        variable var;
        var.set_integer_value(value);
        return var;
    }

    /* ----------------------------------------------------------------------------------------- */

    constant_real_number::constant_real_number(double value) : value(value) {
    }

    std::vector<child_descriptor> constant_real_number::get_children() const {
        return {};
    }

    variable constant_real_number::calc(scope *scope) {
        variable var;
        var.set_real_value(value);
        return var;
    }

    /* ----------------------------------------------------------------------------------------- */

    expression_variable::expression_variable(base_string *name) {
        this->name = name;
        name->add_reference();
    }

    expression_variable::~expression_variable() {
        name->release();
    }

    std::vector<child_descriptor> expression_variable::get_children() const {
        return {};
    }

    variable expression_variable::calc(scope *scope) {
        variable *var = scope->get_attribute(name);
        if (!var) {
            object *ex_object = create_reference_error_clarified_exception(
                scope->get_garbage_collector_data(), name->to_string(nullptr));
            runtime_exception  ex(ex_object);
            ex_object->release();
            throw ex;
        }
        return *var;
    }

    void expression_variable::assign(scope *scope, variable value) {
        scope->set_attribute(name, value);
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

    std::vector<child_descriptor> function_call::get_children() const {
        std::vector<child_descriptor> list;
        for (expression *arg : args) {
            list.push_back({ nullptr, arg });
        }
        return list;
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
        for (auto expr : args) {
            var_list.push_back(expr->calc(scope));
        }
        func->exec(var_list, &ret_val);        
        for (variable &arg : var_list) {
            arg.release();
        }
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

    std::vector<child_descriptor> binary_operation::get_children() const {
        std::vector<child_descriptor> list = {
            { "left", left },
            { "right", right }
        };
        return list;
    }

    variable binary_operation::calc(scope *scope) {
        variable left_value = left->calc(scope);
        variable right_value = right->calc(scope);
        variable result = calc(scope, &left_value, &right_value);
        left_value.release();
        right_value.release();
        return result;
    }

    variable addition::calc(scope *scope, variable *left, variable *right) {
        return left->obj->do_addition(scope->get_garbage_collector_data(), left, right);
    }

    variable subtraction::calc(scope *scope, variable *left, variable *right) {
        return left->obj->do_subtraction(scope->get_garbage_collector_data(), left, right);
    }

    variable multiplication::calc(scope *scope, variable *left, variable *right) {
        return left->obj->do_multiplication(scope->get_garbage_collector_data(), left, right);
    }
}
