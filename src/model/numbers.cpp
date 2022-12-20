/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include <sstream>
#include "numbers.h"
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

    real_number::real_number(gc_data* const gc, const double value) 
            : dynamic_object(gc), value(value) {
    }

    object_type real_number::get_type() const {
        return object_type::number;
    }

    bool real_number::less(const object* const other) const {
        double other_value;
        bool other_is_a_number = other->get_real_value(&other_value);
        assert(other_is_a_number);
        return value < other_value;
    }

    object * real_number::get_first_prototype() const {
        return &real_proto_instance;
    }

    std::wstring real_number::to_string_notation() const {
        return std::to_wstring(value);
    }

    bool real_number::get_real_value(double* const value_ptr) const {
        *value_ptr = value;
        return true;
    }
}
