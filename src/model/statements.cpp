/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

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

    variable_declaration::~variable_declaration() {
        for (descriptor &descr : list) {
            descr.name->release();
            if (descr.init_value) {
                descr.init_value->release();
            }
        }
    }

    void variable_declaration::add_variable(base_string *name, expression *init_value) {
        descriptor descr = {name, init_value};
        list.push_back(descr);
        name->add_reference();
        if (init_value) {
            init_value->add_reference();
        }
    }

    std::vector<std::wstring> variable_declaration::get_list_of_variable_names() const {
        std::vector<std::wstring> result;
        for (auto item : list) {
            result.push_back(item.name->to_string(nullptr));
        }
        return result;
    }

    void variable_declaration::traverse_syntax_tree(element_visitor *visitor) {
        visitor->visit(this);
    }

    const char * variable_declaration::get_class_name() const {
        return "variable declaration";
    }

    std::vector<child_descriptor> variable_declaration::get_children() const {
        return {};
    }

    void variable_declaration::exec(scope *scope) {
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

    unsigned int variable_declaration::generate_node_description(std::stringstream &stream,
            unsigned int *counter, std::unordered_map<element*, unsigned int> &all_indexes) {
        unsigned int stmt_index = ++(*counter);
        all_indexes[this] = stmt_index;
        stream << "  node_" << stmt_index << " [label=<<b>" << get_class_name() << "</b>" 
            << "> color=\"" << get_node_color() << "\"];" << std::endl;
        for (unsigned int k = 0; k < list.size(); k++) {
            descriptor &item = list[k];
            unsigned int var_index = ++(*counter);
            stream << "  node_" << var_index << " [shape=octagon style=\"\" label=<<b>" 
                << encode_utf8(item.name->to_string(nullptr)) << "</b>" << "> color=\"black\"];"
                << std::endl;
            stream << "  node_" << stmt_index << " -> node_" << var_index << " [label=\"  " << k
                << "\"];" << std::endl;
            if (item.init_value) {
                unsigned int init_expr_index =
                    item.init_value->generate_node_description(stream, counter, all_indexes);
                stream << "  node_" << var_index << " -> node_" << init_expr_index << std::endl;
            }            
        }
        return stmt_index;
    }
}
