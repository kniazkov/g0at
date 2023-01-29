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
}
