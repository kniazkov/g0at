/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

namespace goat {
    class object;

    /**
     * @brief A Goat data type descriptor
     */
    class data_type {
    public:
        /**
         * @brief Returns a native C++ data type to which Goat expressions can be cast
         * @return Goat string representing C++ type
         */
        virtual const char * get_cpp_type() const;

        /**
         * @brief Returns the name of native C++ data type to which Goat expressions can be cast
         * @return Name represented as Goat object
         */
        virtual object * get_cpp_type_name() const;

        /**
         * @brief Calculates the data type that will result from the addition of an expression
         *  that has this data type and an expression that has another data type
         * @param right Another data type
         * @return 
         */
        virtual const data_type * do_addition(const data_type * right) const;

        /**
         * @brief Calculates the data type that will result from the subtraction of an expression
         *  that has this data type and an expression that has another data type
         * @param right Another data type
         * @return 
         */
        virtual const data_type * do_subtraction(const data_type * right) const;

        /**
         * @brief Calculates the data type that will result from the multiplication of an
         *  expression that has this data type and an expression that has another data type
         * @param right Another data type
         * @return 
         */
        virtual const data_type * do_multiplication(const data_type * right) const;
    };

    /**
     * @brief Returns pointer to unknown type descriptor
     */
    data_type * get_unknown_data_type();

    /**
     * @brief Returns pointer to invalid type descriptor
     */
    data_type * get_invalid_data_type();

    /**
     * @brief Returns pointer to integer type descriptor
     */
    data_type * get_integer_data_type();

    /**
     * @brief Returns pointer to real number type descriptor
     */
    data_type * get_real_data_type();
}
