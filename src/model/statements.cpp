/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "statements.h"

namespace goat {

    statement_block::~statement_block() {
        for (statement *stmt : list) {
            stmt->release();
        }
    }

    void statement_block::add_statement(statement *stmt) {
        list.push_back(stmt);
        stmt->add_reference();
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

    program::~program() {
        for (const char* name : file_names) {
            delete[] name;
        }
    }

    statement_expression::statement_expression(const char *file_name, unsigned int line,
            expression *expr) : trace_data(file_name, line) {
        this->expr = expr;
        expr->add_reference();
    }

    statement_expression::~statement_expression() {
        expr->release();
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

    declare_variable::~declare_variable() {
        for (descriptor &descr : list) {
            descr.name->release();
            if (descr.init_value) {
                descr.init_value->release();
            }
        }
    }

    void declare_variable::add_variable(base_string *name, expression *init_value) {
        descriptor descr = {name, init_value};
        list.push_back(descr);
        name->add_reference();
        if (init_value) {
            init_value->add_reference();
        }
    }

    std::vector<child_descriptor> declare_variable::get_children() const {
        return {};
    }

    void declare_variable::exec(scope *scope) {
        try {
            for (descriptor &descr : list) {
                if (descr.init_value) {
                    variable value = descr.init_value->calc(scope);
                    scope->set_attribute(descr.name, value);
                    value.release();
                }
                else {
                    scope->set_attribute(descr.name, get_null_object());
                }
            }
        }
        catch(runtime_exception ex) {
            ex.add_stack_trace_data(trace_data);
            throw;
        }
    }
}
