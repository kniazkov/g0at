/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "statements.h"

namespace goat {

    statement_expression::statement_expression(expression *expr) {
        this->expr = expr;
        expr->add_reference();
    }

    statement_expression::~statement_expression() {
        expr->release();
    }

    void statement_expression::exec(scope *scope) {
        variable result = expr->calc(scope);
        result.release();
    }
}
