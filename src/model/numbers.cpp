/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include "numbers.h"
#include "static_object.h"
#include "generic_object.h"

namespace goat {
    /**
     * Prototype object for objects storing or wrapping a number
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

    /**
     * Prototype object for objects storing or wrapping a real number
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
     * @brief Static object that handles real values
     * 
     * The value itself is stored separately in a variable.
     */
    class real_number_handler : public static_object {
    public:
        object_type get_type() const override {
            return object_type::number;
        }

        bool less(const object* const others) const override {
            /*
                This method should never be called for handler objects
            */
            assert(false);
            return false;
        }

        object * get_first_prototype() const override {
            return &real_proto_instance;
        }
        
        std::wstring to_string_notation(const variable* var) const override {
            return std::to_wstring(var->data.double_value);
        }

        bool get_real_value(const variable* var, double* const value_ptr) const override {
            *value_ptr = var->data.double_value;
            return true;
        }
    };

    static real_number_handler real_handler_instance;

    object * get_real_handler() {
        return &real_handler_instance;
    }

    /* ----------------------------------------------------------------------------------------- */

    real_number::real_number(gc_data* const gc, const double value) 
            : dynamic_object(gc), value(value) {
    }

    object_type real_number::get_type() const {
        return object_type::number;
    }

    bool real_number::less(const object* const other) const {
        double other_value;
        bool other_is_a_number = other->get_real_value(nullptr, &other_value);
        assert(other_is_a_number);
        return value < other_value;
    }

    object * real_number::get_first_prototype() const {
        return &real_proto_instance;
    }

    std::wstring real_number::to_string_notation(const variable* var) const {
        return std::to_wstring(value);
    }

    bool real_number::get_real_value(const variable* var, double* const value_ptr) const {
        *value_ptr = value;
        return true;
    }
}
