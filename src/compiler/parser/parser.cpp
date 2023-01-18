/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "parser.h"
#include "compiler/scanner/tokens.h"
#include "model/expressions.h"
#include "model/statements.h"

namespace goat {

    /**
     * @brief Parses an expression that begins with an identifier
     * @return An expression
     */
    expression * parse_expression_begins_with_identifier(token_iterator *iter, token *ident) {
        return nullptr;
    }

    statement * parse_statement(token_iterator *iter) {
        if (!iter->valid()) {
            return nullptr;
        }
        token *tok = iter->get();
        switch(tok->type) {
            case token_type::identifier:
                iter->next();
                return new statement_expression(
                    parse_expression_begins_with_identifier(iter, tok)
                );
        }
        return nullptr;
    }
}
