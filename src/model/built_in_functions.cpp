/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include <cmath>
#include "built_in_functions.h"
#include "exceptions.h"

namespace goat {

    void function_print::exec(std::vector<variable> &args, variable* ret_val) {
        assert(args.size() > 0);
        device->print(args[0].to_string());
    }

    /* ----------------------------------------------------------------------------------------- */

    /**
     * @brief Built-in function, compute square root
     */
    class function_sqrt : public static_function {
    public:
        void exec(std::vector<variable> &args, variable* ret_val) override {
            if (args.size() < 1) {
                throw runtime_exception(get_illegal_agrument_exception());
            }
            double value;
            bool value_is_a_number = args[0].get_real_value(&value);
            if (!value_is_a_number) {
                throw new runtime_exception(get_illegal_agrument_exception());
            }
            ret_val->set_real_value(std::sqrt(value));
        }
    };

    function_sqrt sqrt_instance;
    object * get_sqrt_instance() {
        return &sqrt_instance;
    }
}
