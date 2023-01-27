/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <vector>
#include "code.h"
#include "scope.h"
#include "expressions.h"

namespace goat {

    /**
     * @brief A statement is a syntactic unit that expresses some action to be carried out
     */
    class statement : public element {
    public:
        /**
         * @brief Performs a specific action
         * @param scope The scope in which an action is performed
         */
        virtual void exec(scope *scope) = 0;
    };

    /**
     * @brief Block statement, that is, one that consists of several statements
     */
    class statement_block : public statement {
    public:
        /**
         * @brief Destructor
         */
        ~statement_block();

        /**
         * @brief Adds statement to the list
         * @param stmt statement 
         */
        void add_statement(statement *stmt);

        void exec(scope *scope) override;

    private:
        /**
         * @brief List of statements
         */
        std::vector<statement*> list;
    };

    /**
     * @brief Statement that calculates an expression and then ignores the calculated value
     * 
     * This is the most commonly used statement.
     * Any program consists of expressions whose values are not used.
     * Suppose the expression <code>x = 1</code> (is an assignment), it has the value
     * (namely, 1), but in the statement <code>"x = 1;"</code> this value is not used (ignored),
     * although the current scope changes.
     */
    class statement_expression : public statement {
    public:
        /**
         * @brief Constructor
         * @param expr An expression
         */
        statement_expression(expression *expr);

        /**
         * @brief Destructor
         */
        ~statement_expression();

        void exec(scope *scope) override;

    private:
        /**
         * @brief An expression
         */
        expression *expr;
    };
}
