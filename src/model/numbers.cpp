/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include <cwchar>
#include <cmath>
#include "numbers.h"
#include "generic_object.h"
#include "exceptions.h"

namespace goat {

    /**
     * @brief Converts double value to wide string
     * @param value The value
     * @return String representation of the value
     */
    static std::wstring double_to_string(double value) {
        wchar_t buff[32];
        std::swprintf(buff, 32, L"%g", value);
        return buff;
    }

    template <typename R, typename A> struct unary {
        typedef R ret_type;
        typedef A arg_type;
    };

    template <typename R, typename A> struct pos : public unary <R, A> {
        static R calculate(const A &a) {
            return a;
        }
    };

    template <typename R, typename A> struct neg : public unary <R, A> {
        static R calculate(const A &a) {
            return -a;
        }
    };

    template <typename R, typename A> struct log_not : public unary <R, A> {
        static R calculate(const A &a) {
            return !a;
        }
    };

    template <typename R, typename A> struct inv : public unary <R, A> {
        static R calculate(const A &a) {
            return ~a;
        }
    };

    template <typename R, typename A> struct inc : public unary <R, A> {
        static R calculate(const A &a) {
            return a + 1;
        }
    };

    template <typename R, typename A> struct dec : public unary <R, A> {
        static R calculate(const A &a) {
            return a - 1;
        }
    };

    template <typename R, typename X, typename Y> struct binary {
        typedef R ret_type;
        typedef X left_type;
        typedef Y right_type;
    };

    template <typename R, typename X, typename Y> struct plus : public binary <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x + y;
        }
    };

    template <typename R, typename X, typename Y> struct minus : public binary <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x - y;
        }
    };

    template <typename R, typename X, typename Y> struct mul : public binary <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x * y;
        }
    };

    template <typename R, typename X, typename Y> struct div : public binary <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x / y;
        }
    };

    template <typename R, typename X, typename Y> struct mod : public binary <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x % y;
        }
    };

    template <typename R, typename X, typename Y> struct exp : public binary <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return (R)pow(x, y);
        }
    };

    template <typename R, typename X, typename Y> struct equals : public binary <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x == y;
        }
    };

    template <typename R, typename X, typename Y> struct not_equal : public binary <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x != y;
        }
    };

    template <typename R, typename X, typename Y> struct less : public binary <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x < y;
        }
    };

    template <typename R, typename X, typename Y> struct less_or_equal : public binary <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x <= y;
        }
    };

    template <typename R, typename X, typename Y> struct greater : public binary <R, X, Y> {
        static R calculate(const X &x, const Y &y){
            return x > y;
        }
    };

    template <typename R, typename X, typename Y> struct greater_or_equal 
            : public binary <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x >= y;
        }
    };

    template <typename R, typename X, typename Y> struct bitwise_and : public binary <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x & y;
        }
    };

    template <typename R, typename X, typename Y> struct bitwise_or : public binary <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x | y;
        }
    };

    template <typename R, typename X, typename Y> struct bitwise_xor : public binary <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x ^ y;
        }
    };
    
    /* ----------------------------------------------------------------------------------------- */

    /**
     * @brief Prototype object for objects storing or wrapping a number
     */
    class number_prototype : public generic_static_object {
    public:
        /**
         * @brief Constructor
         */
        number_prototype() {
        }
    };

    static number_prototype number_proto_instance;
    object * get_number_prototype() {
        return &number_proto_instance;
    }

    /* ----------------------------------------------------------------------------------------- */

    /**
     * @brief Prototype object for objects storing or wrapping an integer
     */
    class integer_prototype : public generic_static_object {
    public:
        /**
         * @brief Constructor
         */
        integer_prototype() {
        }

        object * get_first_prototype() const override {
            return &number_proto_instance;
        }
    };

    static integer_prototype integer_proto_instance;
    object * get_integer_prototype() {
        return &integer_proto_instance;
    }

    /**
     * @brief Base object for handling integers
     */
    class integer_number_base : public virtual object {
    public:
        object_type get_type() const override {
            return object_type::number;
        }

        object * get_first_prototype() const override {
            return &integer_proto_instance;
        }
        
        std::wstring to_string_notation(const variable* var) const override {
            return std::to_wstring(get_value(var));
        }

        bool get_integer_value(const variable* var, int64_t* const value_ptr) const override {
            *value_ptr = get_value(var);
            return true;
        }

        bool get_real_value(const variable* var, double* const value_ptr) const override {
            *value_ptr = (double)get_value(var);
            return true;
        }

        template <template<typename R, typename X, typename Y> class F>
        variable binary_math_operation(const variable* left, const variable* right) const {
            int64_t left_value = get_value(left);
            int64_t right_value_integer;
            bool right_is_an_integer = right->get_integer_value(&right_value_integer);
            if (right_is_an_integer) {
                variable result;
                result.set_integer_value(
                    F<int64_t, int64_t, int64_t>::calculate(left_value, right_value_integer)
                );
                return result;
            }
            else {
                double right_value_real;
                bool right_is_a_real_number = right->get_real_value(&right_value_real);
                if (!right_is_a_real_number) {
                    throw runtime_exception(get_illegal_agrument_exception());
                }
                variable result;
                result.set_real_value(
                    F<double, int64_t, double>::calculate(left_value, right_value_real)
                );
                return result;
            }
        }

        variable do_addition(gc_data* const gc,
                const variable* left, const variable* right) const override {
            return binary_math_operation<plus>(left, right);
        }

        variable do_subtraction(gc_data* const gc,
                const variable* left, const variable* right) const override {
            return binary_math_operation<minus>(left, right);
        }

    protected:
        /**
         * @brief Returns the value handled by this object
         * @param var Pointer to a variable to be handled (only for objects
         *   that do not store data themselves)
         * @return Integer value
         */
        virtual int64_t get_value(const variable* var) const = 0;
    };

    /* ----------------------------------------------------------------------------------------- */

    /**
     * @brief Static object that handles integer values
     * 
     * The value itself is stored separately in a variable.
     */
    class integer_number_static : public static_object, public integer_number_base {
    public:
        bool less(const object* const others) const override {
            /*
                This method should never be called for handler objects
            */
            assert(false);
            return false;
        }

    protected:
        int64_t get_value(const variable* var) const override {
            return var->data.int_value;
        }
    };

    static integer_number_static integer_static_instance;
    object * get_integer_handler() {
        return &integer_static_instance;
    }

    /* ----------------------------------------------------------------------------------------- */

    /**
     * @brief Object that stores an integer
     */
    class integer_number_dynamic : public dynamic_object, public integer_number_base {
    private:
        /**
         * @brief The value
         */
        int64_t value;

    public:
        /**
         * @brief Constructor
         * @param gc Data required for the garbage collector
         * @param value The value
         */
        integer_number_dynamic(gc_data* const gc, const double value) 
            : dynamic_object(gc), value(value) {
        }

        bool less(const object* const other) const override {
            double other_value;
            bool other_is_a_number = other->get_real_value(nullptr, &other_value);
            assert(other_is_a_number);
            return value < other_value;
        };

    protected:
        int64_t get_value(const variable* var) const override {
            return value;
        }
    };
    
    object * create_integer_number(gc_data* const gc, const int64_t value) {
        return new integer_number_dynamic(gc, value);
    }

    /* ----------------------------------------------------------------------------------------- */

    /**
     * @brief Prototype object for objects storing or wrapping a real number
     */
    class real_prototype : public generic_static_object {
    public:
        /**
         * @brief Constructor
         */
        real_prototype() {
        }

        object * get_first_prototype() const override {
            return &number_proto_instance;
        }
    };

    static real_prototype real_proto_instance;
    object * get_real_prototype() {
        return &real_proto_instance;
    }

    /**
     * @brief Base object for handling real numbers
     */
    class real_number_base : public virtual object {
    public:
        object_type get_type() const override {
            return object_type::number;
        }

        object * get_first_prototype() const override {
            return &real_proto_instance;
        }
        
        std::wstring to_string_notation(const variable* var) const override {
            return double_to_string(get_value(var));
        }

        bool get_integer_value(const variable* var, int64_t* const value_ptr) const override {
            double value = get_value(var);
            if (value == std::floor(value)) {
                *value_ptr = (int64_t)value;
                return true;
            }
            return false;
        }

        bool get_real_value(const variable* var, double* const value_ptr) const override {
            *value_ptr = get_value(var);
            return true;
        }

        variable do_addition(gc_data* const gc,
                const variable* left, const variable* right) const override {
            double left_value = get_value(left);
            double right_value;
            bool right_is_a_real_number = right->get_real_value(&right_value);
            if (!right_is_a_real_number) {
                throw runtime_exception(get_illegal_agrument_exception());
            }
            variable result;
            result.set_real_value(left_value + right_value);
            return result;
        }
    
    protected:
        /**
         * @brief Returns the value handled by this object
         * @param var Pointer to a variable to be handled (only for objects
         *   that do not store data themselves)
         * @return Real value
         */
        virtual double get_value(const variable* var) const = 0;
    };

    /* ----------------------------------------------------------------------------------------- */

    /**
     * @brief Static object that handles real values
     * 
     * The value itself is stored separately in a variable.
     */
    class real_number_static : public static_object, public real_number_base {
    public:
        bool less(const object* const others) const override {
            /*
                This method should never be called for handler objects
            */
            assert(false);
            return false;
        }

    protected:
        double get_value(const variable* var) const override {
            return var->data.double_value;
        }
    };

    static real_number_static real_static_instance;
    object * get_real_handler() {
        return &real_static_instance;
    }

    /* ----------------------------------------------------------------------------------------- */

    /**
     * @brief Object that stores a real number
     */
    class real_number_dynamic : public dynamic_object, public real_number_base {
    private:
        /**
         * @brief The value
         */
        double value;

    public:
        /**
         * @brief Constructor
         * @param gc Data required for the garbage collector
         * @param value The value
         */
        real_number_dynamic(gc_data* const gc, const double value) 
            : dynamic_object(gc), value(value) {
        }

        bool less(const object* const other) const override {
            double other_value;
            bool other_is_a_number = other->get_real_value(nullptr, &other_value);
            assert(other_is_a_number);
            return value < other_value;
        };

    protected:
        double get_value(const variable* var) const override {
            return value;
        }
    };
    
    object * create_real_number(gc_data* const gc, const double value) {
        return new real_number_dynamic(gc, value);
    }
}
