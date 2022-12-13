/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "dynamic_object.h"

namespace goat {

    /**
     * @brief An "ordinary" object, that is, one whose structure is determined by programmer
     *    when writing the code
     */
    class generic_object : public dynamic_object {
    public:
        /**
         * Constructor
         * @param gc Data required for the garbage collector
         */
        generic_object(gc_data* const gc) :
                dynamic_object(gc) {
        }

        object_type get_type() const override;
        std::wstring to_string_notation() const override;
    };
}
