/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "static_object.h"
#include "dynamic_object.h"

namespace goat {
    
    /**
     * @brief Object that stores an unicode string
     */
    class dynamic_string : public dynamic_object {
    private:
        /**
         * @brief The value
         */
        std::wstring value;

    public:
        /**
         * Constructor
         * @param gc Data required for the garbage collector
         * @param value The value
         */
        dynamic_string(gc_data* const gc, const std::wstring &value) :
                dynamic_object(gc), value(value) {
        }

        object_type get_type() const override;
        bool less(const object* const others) const override;
        std::wstring to_string() const override;
        std::wstring to_string_notation() const override;
        bool get_string_value(std::wstring* const value_ptr) const override;
    };
}
