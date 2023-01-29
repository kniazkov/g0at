/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <vector>
#include <unordered_set>
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
     * @brief Data required when tracing the stack (e.g. when an exception occurs)
     */
    class stack_trace_data {
    public:
        /**
         * @brief Constructor
         * @param file_name The name of the file containing statement
         * @param number The number of the line containing statement
         */
        stack_trace_data(const char *file_name, unsigned int line) 
            : file_name(file_name), line(line) {
        }

    protected:
        /**
         * @brief The name of the file containing statement
         */
        const char *file_name;

        /**
         * @brief The number of the line containing statement
         */
        unsigned int line;
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
     * @brief The entire program
     */
    class program : public statement_block {
    public:
        /**
         * @brief Destructor
         */
        ~program();

        /**
         * @brief Returns the pointer to the set of objects so that we can add objects there
         *   while parsing
         * @return Pointer to the set of objects
         */
        inline std::unordered_set<object*> * get_object_set() {
            return &objects;
        }

        /**
         * @brief Returns the pointer to the vector that contains copies of the names of
         *   the source files from which the program is compiled
         * @return Pointer to the vector containing file names (C-style strings)
         */
        inline std::vector<const char*> * get_file_names_list() {
            return &file_names;
        }

    private:
        /**
         * @brief Set of objects that are created during parsing (needed to mark dynamic objects)
         */
        std::unordered_set<object*> objects;

        /**
         * @brief Copies of the names of the source files from which the program is compiled
         */
        std::vector<const char*> file_names;
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
    class statement_expression : public statement, public stack_trace_data {
    public:
        /**
         * @brief Constructor
         * @param file_name The name of the file containing this statement
         * @param number The number of the line containing this statement
         * @param expr An expression
         */
        statement_expression(const char *file_name, unsigned int line, expression *expr);

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
