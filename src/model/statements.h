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
#include "exceptions.h"

namespace goat {

    class base_string;

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
        
        const char * get_node_color() const override;
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

        const char * get_class_name() const override;
        std::vector<child_descriptor> get_children() const override;
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

        const char * get_class_name() const override;
        
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
    class statement_expression : public statement {
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

        const char * get_class_name() const override;
        std::vector<child_descriptor> get_children() const override;
        void exec(scope *scope) override;

    private:
        /**
         * @brief An expression
         */
        expression *expr;

        /**
         * @brief Data required when tracing the stack (e.g. when an exception occurs)
         */
        stack_trace_data trace_data;
    };

    /**
     * @brief Statement describing the declaration of variables
     */
    class declare_variable : public statement {
    public:
        /**
         * @brief Constructor
         * @param file_name The name of the file containing this statement
         * @param number The number of the line containing this statement
         */
        declare_variable(const char *file_name, unsigned int line) : trace_data(file_name, line) {
        }

        /**
         * @brief Destructor
         */
        ~declare_variable();

        /**
         * @brief Adds a variable to the list
         * @param name Variable name
         * @param init_value Initial value of the variable (can be <code>nullptr</code>)
         */
        void add_variable(base_string *name, expression *init_value);

        const char * get_class_name() const override;
        std::vector<child_descriptor> get_children() const override;
        void exec(scope *scope) override;

    private:
        /**
         * @brief Descriptor of one variable
         */
        struct descriptor {
            /**
             * @brief Variable name
             */
            base_string *name;

            /**
             * @brief Initial value of the variable
             */
            expression *init_value;
        };

        /**
         * @brief List containing variable descriptors
         */
        std::vector<descriptor> list;

        /**
         * @brief Data required when tracing the stack (e.g. when an exception occurs)
         */
        stack_trace_data trace_data;
    };
}
