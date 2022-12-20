/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include <cmath>
#include "built_in_functions.h"

namespace goat {

    /**
     * @brief Built-in function, compute square root
     */
    class function_sqrt : public static_function {
        void exec(std::vector<variable*> &args, variable* ret_val) {
            assert(args.size() > 0);
            double value;
            bool value_is_a_number = args[0]->get_real_value(&value);
            assert(value_is_a_number);
            ret_val->set_real_value(std::sqrt(value));
        }
    };

    function_sqrt sqrt_instance;
    base_function * get_sqrt_instance() {
        return &sqrt_instance;
    }
}