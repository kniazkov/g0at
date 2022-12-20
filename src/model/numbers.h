/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "dynamic_object.h"

namespace goat {

    /**
     * @brief Object that stores a real number
     */
    class real_number : public dynamic_object {
    private:
        /**
         * @brief The value
         */
        double value;

    public:
        /**
         * Constructor
         * @param gc Data required for the garbage collector
         * @param value The value
         */
        real_number(gc_data* const gc, const double value);

        object_type get_type() const override;
        bool less(const object* const others) const override;
        object *get_first_prototype() const override;
        std::wstring to_string_notation(const variable* var) const override;
        bool get_real_value(const variable* var, double* const value_ptr) const override;
    };

    /**
     * @return Pointer to the prototype of number objects
     */
    object * get_number_prototype();

    /**
     * @return Pointer to the prototype of real number objects
     */
    object * get_real_prototype();

    /**
     * @return Pointer to the static object that handles real values
     */
    object * get_real_handler();
}
