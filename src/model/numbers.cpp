/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include <cwchar>
#include "numbers.h"
#include "generic_object.h"

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

        bool get_real_value(const variable* var, double* const value_ptr) const override {
            *value_ptr = get_value(var);
            return true;
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
