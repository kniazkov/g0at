/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <vector>
#include "static_object.h"
#include "dynamic_object.h"

namespace goat {

    /**
     * @brief A function is an object that, in addition to storing data, can perform some action
     */
    class function : public virtual object {
    public:
        /**
         * @brief Execute function, i.e., perform some action
         * @param args Arguments of the function
         * @param ret_val [out] Return value
         */
        virtual void exec(std::vector<variable*> &args, variable* ret_val) = 0;
    };
}