/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "data_type.h"
#include "model/strings.h"

namespace goat {

    /**
     * @brief Descriptor of 'unknown' data type
     * 
     * The unknown data type is used for variables that have not been assigned a value
     */
    class unknown_data_type : public data_type {
    };

    static unknown_data_type _UNKNOWN;
    data_type * get_unknown_data_type() {
        return &_UNKNOWN;
    }

    /* ---------------------------------------------------------------------------------------- */

    /**
     * @brief Ivalid data type descriptor
     * 
     * The invalid data type is used to generate errors in static code analysis
     */
    class invalid_data_type : public data_type {
    };

    static invalid_data_type _INVALID;
    data_type * get_invalid_data_type() {
        return &_INVALID;
    }

    /* ---------------------------------------------------------------------------------------- */

    static_string integer_type_name(L"int64_t");

    /**
     * @brief Integer data type descriptor
     */
    class integer_data_type : public data_type {
    public:
        const char * get_cpp_type() const override {
            return "int64_t";
        }

        object * get_cpp_type_name() const override {
            return &integer_type_name;
        }

        const data_type * do_addition(const data_type * right) const override {
            return do_arithmetic_operation(right);
        }

        const data_type * do_subtraction(const data_type * right) const override {
            return do_arithmetic_operation(right);
        }

        const data_type * do_multiplication(const data_type * right) const override {
            return do_arithmetic_operation(right);
        }

    private:
        /**
         * @brief Calculates the data type that will result from on arithmetic operation
         *  of an expression that has this data type and an expression that has another data type
         * @param right Another data type
         * @return 
         */
        const data_type * do_arithmetic_operation(const data_type * right) const;
    };

    static integer_data_type _INTEGER;
    data_type * get_integer_data_type() {
        return &_INTEGER;
    }

    /* ---------------------------------------------------------------------------------------- */

    static_string real_type_name(L"double");

    /**
     * @brief Real number data type descriptor
     */
    class real_data_type : public data_type {
    public:
        const char * get_cpp_type() const override {
            return "double";
        }

        object * get_cpp_type_name() const override {
            return &real_type_name;
        }

        const data_type * do_addition(const data_type * right) const override {
            return do_arithmetic_operation(right);
        }

        const data_type * do_subtraction(const data_type * right) const override {
            return do_arithmetic_operation(right);
        }

        const data_type * do_multiplication(const data_type * right) const override {
            return do_arithmetic_operation(right);
        }
    
    private:
        /**
         * @brief Calculates the data type that will result from on arithmetic operation
         *  of an expression that has this data type and an expression that has another data type
         * @param right Another data type
         * @return 
         */
        const data_type * do_arithmetic_operation(const data_type * right) const;
    };

    static real_data_type _REAL;
    data_type * get_real_data_type() {
        return &_REAL;
    }

    /* ---------------------------------------------------------------------------------------- */

    const char * data_type::get_cpp_type() const {
        return nullptr;
    }

    object * data_type::get_cpp_type_name() const {
        return nullptr;
    }

    const data_type * data_type::do_addition(const data_type * right) const {
        return &_INVALID;
    }

    const data_type * data_type::do_subtraction(const data_type * right) const {
        return &_INVALID;
    }

    const data_type * data_type::do_multiplication(const data_type * right) const {
        return &_INVALID;
    }

    const data_type * integer_data_type::do_arithmetic_operation(const data_type * right) const {
        if (right == &_INTEGER) {
            return &_INTEGER;
        }
        if (right == &_REAL) {
            return &_REAL;
        }
        return &_INVALID;
    }

    const data_type * real_data_type::do_arithmetic_operation(const data_type * right) const {
        if (right == &_INTEGER || right == &_REAL) {
            return &_REAL;
        }
        return &_INVALID;
    }
}
