/*
    Copyright 2022 Ivan Kniazkov

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
     * @brief Expression representing a constant real number
     */
    class constant_real_number : public expression {
    public:
        /**
         * @brief Constructor
         * @param value The value
         */
        constant_real_number(double value);

        variable calc(scope *scope) override;

    private:
        /**
         * @brief The value
         */
        double value;
    };

    /**
     * @brief Searches for a variable in scope and returns its value
     */
    class read_variable : public expression {
    public:
        /**
         * @brief Constructor
         * @param name The variable name
         */
        read_variable(base_string *name);

        /**
         * @brief Destructor
         */
        ~read_variable();

        variable calc(scope *scope) override;

    private:
        /**
         * @brief The variable name
         */
        base_string *name;
    };

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
}
