/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "parser.h"
#include "compiler/scanner/tokens.h"
#include "compiler/common/exceptions.h"
#include "resources/messages.h"
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
            case token_type::identifier: {
                iter->next();
                expression *expr = parse_expression_begins_with_identifier(iter, tok);
                if (iter->valid()) {
                    tok = iter->get();
                    if (tok->type == token_type::semicolon) {
                        iter->next();
                    }
                }
                return new statement_expression(expr);
            }
        }
        throw compiler_exception(tok, get_messages()->msg_unable_to_parse_token_sequence());
    }
}
