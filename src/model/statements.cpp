/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include "statements.h"
#include "lib/utf8_encoder.h"

namespace goat {

    const char * statement::get_node_color() const {
        return "blue";
    }

    std::vector<element_data_descriptor> statement::get_data() const {
        return {};
    }

    /* ----------------------------------------------------------------------------------------- */

    statement_block::~statement_block() {
        for (statement *stmt : list) {
            stmt->release();
        }
    }

    void statement_block::add_statement(statement *stmt) {
        list.push_back(stmt);
        stmt->add_reference();
    }

    void statement_block::traverse_syntax_tree(element_visitor *visitor) {
        for (statement *stmt : list) {
            stmt->traverse_syntax_tree(visitor);
        }
    }

    const char * statement_block::get_class_name() const {
        return "block statement";
    }

    std::vector<child_descriptor> statement_block::get_children() const {
        std::vector<child_descriptor> result;
        for (statement *stmt : list) {
            result.push_back({ nullptr, stmt });
        }
        return result;
    }
    
    void statement_block::exec(scope *scope) {
        for (statement *stmt : list) {
            stmt->exec(scope);
        }
    }

    /* ----------------------------------------------------------------------------------------- */

    program::~program() {
        for (const char* name : file_names) {
            delete[] name;
        }
    }

    const char * program::get_class_name() const {
        return "program";
    }

    /* ----------------------------------------------------------------------------------------- */

    statement_expression::statement_expression(const char *file_name, unsigned int line,
            expression *expr) : trace_data(file_name, line) {
        this->expr = expr;
        expr->add_reference();
    }

    statement_expression::~statement_expression() {
        expr->release();
    }

    void statement_expression::traverse_syntax_tree(element_visitor *visitor) {
        expr->traverse_syntax_tree(visitor);
    }

    const char * statement_expression::get_class_name() const {
        return "statement expression";
    }

    std::vector<child_descriptor> statement_expression::get_children() const {
        std::vector<child_descriptor> list = {
            { "expression", expr }
        };
        return list;
    }

    void statement_expression::exec(scope *scope) {
        try {
            variable result = expr->calc(scope);
            result.release();
        }
        catch(runtime_exception ex) {
            ex.add_stack_trace_data(trace_data);
            throw;
        }
    }

    /* ----------------------------------------------------------------------------------------- */

    data_descriptor::data_descriptor(bool modifiable, base_string *name, base_string *proto_name,
            expression *init_value) {
        this->modifiable = modifiable;
        this->name = name;
        name->add_reference();
        this->proto_name = proto_name;
        if (proto_name) {
            proto_name->add_reference();
        }
        this->init_value = init_value;
        if (init_value) {
            init_value->add_reference();
        }
        this->type = get_unknown_data_type();
    }

    data_descriptor::~data_descriptor() {
        name->release();
        if (proto_name) {
            proto_name->release();
        }
        if (init_value) {
            init_value->release();
        }
    }

    void data_descriptor::initialize_data_in_the_scope(scope *scope) {
        if (init_value) {
            variable value = init_value->calc(scope);
            scope->set_attribute(name, value);
            value.release();
        }
        else {
            scope->set_attribute(name, get_null_object());
        }
    }

    void data_descriptor::traverse_syntax_tree(element_visitor *visitor) {
        visitor->visit(this);
        if (init_value) {
            init_value->traverse_syntax_tree(visitor);
        }
    }

    const char * data_descriptor::get_class_name() const {
        return "data";
    }

    std::vector<child_descriptor> data_descriptor::get_children() const {
        std::vector<child_descriptor> list;
        if (init_value) {
            list.push_back({"init value", init_value});
        }
        return list;
    }

    std::vector<element_data_descriptor> data_descriptor::get_data() const {
        std::vector<element_data_descriptor> list;
        variable var;
        var.obj = name;
        list.push_back({"name", var});
        return list;
    }

    /* ----------------------------------------------------------------------------------------- */

    variable_declaration::~variable_declaration() {
        for (data_descriptor *descriptor : list) {
            descriptor->release();
        }
    }

    /**
     * @todo Check for duplicated names 
     */
    void variable_declaration::add_variable(data_descriptor *descriptor) {
        list.push_back(descriptor);
        map[descriptor->get_name_as_string()] = descriptor;
        descriptor->add_reference();
    }

    std::vector<std::wstring> variable_declaration::get_list_of_variable_names() const {
        std::vector<std::wstring> result;
        for (auto descriptor : list) {
            result.push_back(descriptor->get_name_as_string());
        }
        return result;
    }

    data_descriptor * variable_declaration::get_descriptor_by_name(std::wstring name) {
        auto pair = map.find(name);
        assert(pair != map.end());
        return pair->second;
    }

    void variable_declaration::traverse_syntax_tree(element_visitor *visitor) {
        visitor->visit(this);
        for (auto descriptor : list) {
            descriptor->traverse_syntax_tree(visitor);
        }
    }

    const char * variable_declaration::get_class_name() const {
        return "variable declaration";
    }

    std::vector<child_descriptor> variable_declaration::get_children() const {
        return {};
    }

    void variable_declaration::exec(scope *scope) {
        try {
            for (auto descriptor : list) {
                descriptor->initialize_data_in_the_scope(scope);
            }
        }
        catch(runtime_exception ex) {
            ex.add_stack_trace_data(trace_data);
            throw;
        }
    }
}
