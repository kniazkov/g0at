/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <string>
#include "functions.h"

namespace goat {

    /**
     * @brief Built-in function, prints a string to the console
     */
    class function_print : public dynamic_object, public base_function {
    public:
        /**
         * @brief An abstract printer that actually does the printing
         */
        struct printer {
            /**
             * @brief Prints a string
             * @param str A string
             */
            virtual void print(std::wstring str) = 0;
        };

        /**
         * @brief Constructor
         * @param gc Data required for the garbage collector
         * @param device The printer that actually does the printing
         */
        function_print(gc_data *gc, printer *device) 
                : dynamic_object(gc), device(device) {
        }

        void exec(std::vector<variable> &args, variable* ret_val) override;

    protected:
        /**
         * @brief The printer that actually does the printing
         */
        printer *device;
    };

    /**
     * @return Pointer to the function that calculates square root
     */
    object * get_sqrt_instance();
}
