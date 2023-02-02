/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include "expressions.h"
#include "functions.h"
#include "exceptions.h"
#include "statements.h"

namespace goat {

    const char * expression::get_node_color() const {
        return "darkgreen";
    }

    assignable_expression * expression::to_assignable_expression() {
        return nullptr;
    }

    const char * assignable_expression::get_node_color() const {
        return "olive";
    }

    assignable_expression * assignable_expression::to_assignable_expression() {
        return this;
    }

    /* ----------------------------------------------------------------------------------------- */

    object_as_expression::object_as_expression(object *obj) {
        this->obj = obj;
        obj->add_reference();
    }

    object_as_expression::~object_as_expression() {
        obj->release();
    }

    const char * object_as_expression::get_class_name() const {
        return "object as expression";
    }

    std::vector<child_descriptor> object_as_expression::get_children() const {
        return {};
    }

    std::vector<element_data_descriptor> object_as_expression::get_data() const {
        std::vector<element_data_descriptor> list;
        variable var;
        var.obj = obj;
        list.push_back({ "object", var });
        return list;
    }

    variable object_as_expression::calc(scope *scope) {
        obj->add_reference();
        variable var;
        var.obj = obj;
        return var;
    }

    /* ----------------------------------------------------------------------------------------- */

    constant_integer_number::constant_integer_number(int64_t value) : value(value) {
    }

    const char * constant_integer_number::get_class_name() const {
        return "integer";
    }

    std::vector<child_descriptor> constant_integer_number::get_children() const {
        return {};
    }

    std::vector<element_data_descriptor> constant_integer_number::get_data() const {
        std::vector<element_data_descriptor> list;
        variable var;
        var.set_integer_value(value);
        list.push_back({ "value", var });
        return list;
    }

    variable constant_integer_number::calc(scope *scope) {
        variable var;
        var.set_integer_value(value);
        return var;
    }

    /* ----------------------------------------------------------------------------------------- */

    constant_real_number::constant_real_number(double value) : value(value) {
    }

    const char * constant_real_number::get_class_name() const {
        return "real";
    }

    std::vector<child_descriptor> constant_real_number::get_children() const {
        return {};
    }

    std::vector<element_data_descriptor> constant_real_number::get_data() const {
        std::vector<element_data_descriptor> list;
        variable var;
        var.set_real_value(value);
        list.push_back({ "value", var });
        return list;
    }

    variable constant_real_number::calc(scope *scope) {
        variable var;
        var.set_real_value(value);
        return var;
    }

    /* ----------------------------------------------------------------------------------------- */

    expression_variable::expression_variable(base_string *name) {
        this->name = name;
        this->declaration = nullptr;
        name->add_reference();
    }

    expression_variable::~expression_variable() {
        name->release();
    }

    void expression_variable::traverse_syntax_tree(element_visitor *visitor) {
        visitor->visit(this);
    }

    const char * expression_variable::get_class_name() const {
        return "variable";
    }

    std::vector<child_descriptor> expression_variable::get_children() const {
        return {};
    }

    std::vector<element_data_descriptor> expression_variable::get_data() const {
        std::vector<element_data_descriptor> list;
        variable var;
        var.obj = name;
        list.push_back({ "name", var });
        return list;
    }

    void expression_variable::generate_additional_edges(std::stringstream &stream,
            unsigned int index, std::unordered_map<element*, unsigned int> &all_indexes) {
        if (declaration) {
            auto pair = all_indexes.find(declaration);
            if (pair != all_indexes.end()) {
                stream << "  node_" << pair->second << " -> node_" << index 
                    << "  [style=dashed dir=back color=midnightblue];"
                    << std::endl;
            }
        }
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

    void function_call::traverse_syntax_tree(element_visitor *visitor) {
        for (auto arg : args) {
            arg->traverse_syntax_tree(visitor);
        }
    }

    const char * function_call::get_class_name() const {
        return "function call";
    }

    std::vector<child_descriptor> function_call::get_children() const {
        std::vector<child_descriptor> list;
        for (expression *arg : args) {
            list.push_back({ nullptr, arg });
        }
        return list;
    }

    std::vector<element_data_descriptor> function_call::get_data() const {
        std::vector<element_data_descriptor> list;
        variable var;
        var.obj = name;
        list.push_back({ "name", var });
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

    void binary_operation::traverse_syntax_tree(element_visitor *visitor) {
        left->traverse_syntax_tree(visitor);
        right->traverse_syntax_tree(visitor);
    }

    std::vector<child_descriptor> binary_operation::get_children() const {
        std::vector<child_descriptor> list = {
            { "left", left },
            { "right", right }
        };
        return list;
    }

    std::vector<element_data_descriptor> binary_operation::get_data() const {
        return {};
    }

    variable binary_operation::calc(scope *scope) {
        variable left_value = left->calc(scope);
        variable right_value = right->calc(scope);
        variable result = calc(scope, &left_value, &right_value);
        left_value.release();
        right_value.release();
        return result;
    }

    const char * addition::get_class_name() const {
        return "addition";
    }

    variable addition::calc(scope *scope, variable *left, variable *right) {
        return left->obj->do_addition(scope->get_garbage_collector_data(), left, right);
    }

    const char * subtraction::get_class_name() const {
        return "subtraction";
    }

    variable subtraction::calc(scope *scope, variable *left, variable *right) {
        return left->obj->do_subtraction(scope->get_garbage_collector_data(), left, right);
    }

    const char * multiplication::get_class_name() const {
        return "multiplication";
    }

    variable multiplication::calc(scope *scope, variable *left, variable *right) {
        return left->obj->do_multiplication(scope->get_garbage_collector_data(), left, right);
    }

    /* ----------------------------------------------------------------------------------------- */

    assignment::assignment(assignable_expression *left, expression *right) {
        this->left = left;
        left->add_reference();
        this->right = right;
        right->add_reference();
    }

    assignment::~assignment() {
        left->release();
        right->release();
    }

    void assignment::traverse_syntax_tree(element_visitor *visitor) {
        left->traverse_syntax_tree(visitor);
        right->traverse_syntax_tree(visitor);
    }

    const char * assignment::get_node_color() const {
        return "darkred";
    }

    std::vector<child_descriptor> assignment::get_children() const {
        std::vector<child_descriptor> list = {
            { "left", left },
            { "right", right }
        };
        return list;
    }

    std::vector<element_data_descriptor> assignment::get_data() const {
        return {};
    }

    variable assignment::calc(scope *scope) {
        variable right_value = right->calc(scope);
        variable result = calc(scope, left, &right_value);
        right_value.release();
        return right_value;
    }

    const char * simple_assignment::get_class_name() const {
        return "simple assignment";
    }

    variable simple_assignment::calc(scope *scope, assignable_expression *left, variable *right) {
        left->assign(scope, *right);
        right->add_reference();
        return *right;
    }
}