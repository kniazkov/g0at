/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <vector>
#include "code.h"
#include "scope.h"
#include "strings.h"
#include "compiler/analyzer/data_type.h"

namespace goat {

    class assignable_expression;
    class variable_declaration;
    class statement_block;

    /**
     * @brief An expression is a syntactic construction that returns (calculates) some value
     */
    class expression : public element {
    public:
        /**
         * @brief Calculates the value of the expression
         * @param scope The scope in which the expression is calculated
         * @return Calculated expression
         */
        virtual variable calc(scope *scope) = 0;

        /**
         * @brief Tries to cast an expression to assignable
         * @return Pointer to assignable expression or <code>nullptr</code> if the expression
         *   is not assignable
         */
        virtual assignable_expression * to_assignable_expression();

        /**
         * @brief Calculates a Goat data type to which this expressions can be cast
         * @return Goat data type
         */
        virtual const data_type * get_data_type() const;

        /**
         * @brief Forms a list containing data descriptors that are in all expressions
         * @return A list containing descriptors of additional data
         */
        std::vector<element_data_descriptor> get_common_data() const;

        const char * get_node_color() const override;
        const char * get_background_color() const override;
        std::vector<element_data_descriptor> get_data() const override;
    };

    /**
     * @brief An expression whose value can be changed (assigned a different value),
     *   e.g. a variable
     */
    class assignable_expression : public expression {
    public:
        /**
         * @brief Assigns a new value to the expression
         * @param scope The scope in which the expression is calculated
         * @param value New value
         */
        virtual void assign(scope *scope, variable value) = 0;

        /**
         * @brief Sets a new data type (usually when a variable is assigned)
         * @param type Goat data type
         */
        virtual void set_data_type(const data_type *type) = 0;

        assignable_expression * to_assignable_expression() override;
        const char * get_node_color() const override;
    };

    /**
     * @brief Expression representing an object (just returns this object)
     * 
     * In this way we can wrap any object, including strings, into an expression.
     */
    class object_as_expression : public expression {
    public:
        /**
         * @brief Constructor
         * @param value The value
         */
        object_as_expression(object *obj);

        /**
         * @brief Destructor
         */
        ~object_as_expression();

        const char * get_class_name() const override;
        std::vector<child_descriptor> get_children() const override;
        std::vector<element_data_descriptor> get_data() const override;
        variable calc(scope *scope) override;

    private:
        /**
         * @brief The object
         */
        object *obj;
    };
    
    /**
     * @brief Expression representing a constant integer number
     */
    class constant_integer_number : public expression {
    public:
        /**
         * @brief Constructor
         * @param value The value
         */
        constant_integer_number(int64_t value);

        const char * get_class_name() const override;
        std::vector<child_descriptor> get_children() const override;
        std::vector<element_data_descriptor> get_data() const override;
        const data_type * get_data_type() const override;
        variable calc(scope *scope) override;

    private:
        /**
         * @brief The value
         */
        int64_t value;
    };
    
    /**
     * @brief Expression representing a constant real number
     */
    class constant_real_number : public expression {
    public:
        /**
         * @brief Constructor
         * @param value The value
         */
        constant_real_number(double value);

        const char * get_class_name() const override;
        std::vector<child_descriptor> get_children() const override;
        std::vector<element_data_descriptor> get_data() const override;
        const data_type * get_data_type() const override;
        variable calc(scope *scope) override;

    private:
        /**
         * @brief The value
         */
        double value;
    };

    /**
     * @brief An expression that represents accessing to a variable (i.e. reading and writing)
     */
    class expression_variable : public assignable_expression {
    public:
        /**
         * @brief Constructor
         * @param name The variable name
         */
        expression_variable(base_string *name);

        /**
         * @brief Destructor
         */
        ~expression_variable();

        /**
         * @brief Returns the name of the variable
         * @return Variable name
         */
        std::wstring get_variable_name() const {
            return name->to_string(nullptr);
        }

        /**
         * @brief Sets the pointer to statement in which the variable has been declared
         * @param stmt The statement
         */
        void set_declaration_statement(variable_declaration *stmt) {
            declaration = stmt;
        }

        void traverse_syntax_tree(element_visitor *visitor) override;
        const char * get_class_name() const override;
        std::vector<child_descriptor> get_children() const override;
        std::vector<element_data_descriptor> get_data() const override;
        void generate_additional_edges(std::stringstream &stream, unsigned int index,
            std::unordered_map<element*, unsigned int> &all_indexes) override;
        variable calc(scope *scope) override;
        void assign(scope *scope, variable value) override;
        const data_type * get_data_type() const override;
        void set_data_type(const data_type *type) override;

    private:
        /**
         * @brief The variable name
         */
        base_string *name;

        /**
         * @brief Statement in which the variable has been declared
         */
        variable_declaration *declaration;
    };

    /**
     * @brief Searches for a function in scope and invokes it
     */
    class function_call : public expression {
    public:
        /**
         * @brief Constructor
         * @param name Function name (Name of the variable that contains the function object)
         * @param args Function arguments
         */
        function_call(base_string *name, std::vector<expression*> &args);
        /**
         * @brief Destructor
         */
        ~function_call();

        void traverse_syntax_tree(element_visitor *visitor) override;
        const char * get_class_name() const override;
        std::vector<child_descriptor> get_children() const override;
        std::vector<element_data_descriptor> get_data() const override;
        variable calc(scope *scope) override;

    private:
        /**
         * @brief Function name
         */
        base_string *name;

        /**
         * @brief Function arguments
         */
        std::vector<expression*> args;
    };

    /**
     * @brief An operation that has two operands
     */
    class binary_operation : public expression {
    public:
        /**
         * @brief Constructor
         * @param left Left operand
         * @param right Right operand
         */
        binary_operation(expression *left, expression *right);

        /**
         * @brief Destructor
         */
        ~binary_operation();

        void traverse_syntax_tree(element_visitor *visitor) override;
        std::vector<child_descriptor> get_children() const override;
        variable calc(scope *scope) override;
        const data_type * get_data_type() const override;

    protected:
        /**
         * @brief Calculates the value of the expression
         * @param scope The scope in which the expression is calculated
         * @param left Left operand
         * @param right Right operand
         * @return Calculated expression
         */
        virtual variable calc(scope *scope, variable *left, variable *right) = 0;

        /**
         * @brief Calculates a Goat data type to which this bunary operation can be cast
         * @param left The type of the left operand
         * @param right The type of the right operand
         * @return Native C++ data type
         */
        virtual const data_type * get_data_type(
            const data_type *left, const data_type *right) const = 0;

    private:
        /**
         * @brief Left operand
         */
        expression *left;

        /**
         * @brief Right operand
         */
        expression *right;
    };

    /**
     * @brief Addition
     */
    class addition : public binary_operation {
    public:
        /**
         * @brief Constructor
         * @param left Left operand
         * @param right Right operand
         */
        addition(expression *left, expression *right) : binary_operation(left, right) {
        }

        /**
         * @brief Functor that creates this object
         * @param left Left operand
         * @param right Right operand
         * @return Binary operation object
         */
        static binary_operation * creator(expression *left, expression *right) {
            return new addition(left, right);
        }

        const char * get_class_name() const override;

    protected:
        variable calc(scope *scope, variable *left, variable *right) override;
        const data_type * get_data_type(
            const data_type *left, const data_type *right) const override;
    };

    /**
     * @brief Subtraction
     */
    class subtraction : public binary_operation {
    public:
        /**
         * @brief Constructor
         * @param left Left operand
         * @param right Right operand
         */
        subtraction(expression *left, expression *right) : binary_operation(left, right) {
        }

        /**
         * @brief Functor that creates this object
         * @param left Left operand
         * @param right Right operand
         * @return Binary operation object
         */
        static binary_operation * creator(expression *left, expression *right) {
            return new subtraction(left, right);
        }

        const char * get_class_name() const override;

    protected:
        variable calc(scope *scope, variable *left, variable *right) override;
        const data_type * get_data_type(
            const data_type *left, const data_type *right) const override;
    };

    /**
     * @brief Mutliplication
     */
    class multiplication : public binary_operation {
    public:
        /**
         * @brief Constructor
         * @param left Left operand
         * @param right Right operand
         */
        multiplication(expression *left, expression *right) : binary_operation(left, right) {
        }

        /**
         * @brief Functor that creates this object
         * @param left Left operand
         * @param right Right operand
         * @return Binary operation object
         */
        static binary_operation * creator(expression *left, expression *right) {
            return new multiplication(left, right);
        }

    const char * get_class_name() const override;

    protected:
        variable calc(scope *scope, variable *left, variable *right) override;
        const data_type * get_data_type(
            const data_type *left, const data_type *right) const override;
    };

    /**
     * @brief Assignment operation
     */
    class assignment : public expression {
    public:
        /**
         * @brief Constructor
         * @param left Left operand
         * @param right Right operand
         */
        assignment(assignable_expression *left, expression *right);

        /**
         * @brief Destructor
         */
        ~assignment();

        void traverse_syntax_tree(element_visitor *visitor) override;
        const char * get_node_color() const override;
        std::vector<child_descriptor> get_children() const override;
        variable calc(scope *scope) override;

    protected:
        /**
         * @return Left expression
         */
        assignable_expression * get_left_expression() const {
            return left;
        }

        /**
         * @brief Returns the type of the right operand
         * @return Goat data type
         */
        const data_type * get_right_type() const;

        /**
         * @brief Calculates the value of the expression
         * @param scope The scope in which the expression is calculated
         * @param left Left operand
         * @param right Right operand
         * @return Calculated expression
         */
        virtual variable calc(scope *scope, assignable_expression *left, variable *right) = 0;

    private:
        /**
         * @brief Left operand
         */
        assignable_expression *left;

        /**
         * @brief Right operand
         */
        expression *right;
    };

    /**
     * @brief Simple assignment
     */
    class simple_assignment : public assignment {
    public:
        /**
         * @brief Constructor
         * @param left Left operand
         * @param right Right operand
         */
        simple_assignment(assignable_expression *left, expression *right) 
                : assignment(left, right) {
        }

        /**
         * @brief Functor that creates this object
         * @param left Left operand
         * @param right Right operand
         * @return Binary operation object
         */
        static assignment * creator(assignable_expression *left, expression *right) {
            return new simple_assignment(left, right);
        }

        const char * get_class_name() const override;
        const data_type * get_data_type() const override;

    protected:
        variable calc(scope *scope, assignable_expression *left, variable *right) override;
    };

    /**
     * @brief Function declaration
     */
    class function_declaration : public expression {
        friend class user_defined_function;
    public:
        /**
         * @brief Constructor
         * @param args Function arguments
         * @param body Function body
         */
        function_declaration(std::vector<base_string*> args, statement_block *body);

        /**
         * @brief Destructor
         */
        ~function_declaration();

        const char * get_class_name() const override;
        const data_type * get_data_type() const override;
        void traverse_syntax_tree(element_visitor *visitor) override;
        std::vector<child_descriptor> get_children() const override;
        std::vector<element_data_descriptor> get_data() const override;
        variable calc(scope *scope) override;

    private:
        /**
         * @brief Function arguments
         */
        std::vector<base_string*> args;

        /**
         * @brief Function body
         */
        statement_block *body;
    };
}
