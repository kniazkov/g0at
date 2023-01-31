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

namespace goat {

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
    };

    /**
     * @brief An expression whose value can be changed (assigned a different value),
     *   e.g. a variable
     */
    class assignable_expression : public expression {
        /**
         * @brief Assigns a new value to the expression
         * @param scope The scope in which the expression is calculated
         * @param value New value
         */
        virtual void assign(scope *scope, variable value) = 0;
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

        const char * get_class_name() const override;
        std::vector<child_descriptor> get_children() const override;
        variable calc(scope *scope) override;
        void assign(scope *scope, variable value) override;

    private:
        /**
         * @brief The variable name
         */
        base_string *name;
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

        const char * get_class_name() const override;
        std::vector<child_descriptor> get_children() const override;
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

        std::vector<child_descriptor> get_children() const override;
        variable calc(scope *scope) override;

    protected:
        /**
         * @brief Calculates the value of the expression
         * @param scope The scope in which the expression is calculated
         * @param left Left operand
         * @param right Right operand
         * @return Calculated expression
         */
        virtual variable calc(scope *scope, variable *left, variable *right) = 0;

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
    };
}
