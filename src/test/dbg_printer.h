/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <sstream>
#include "model/built_in_functions.h"

namespace goat {

    /**
     * @brief Debug printer that prints to the buffer
     */
    struct dbg_printer : public function_print::printer {
        /**
         * The buffer
         */
        std::wstringstream stream;

        void print(std::wstring str) override {
            stream << str;
        }
    };
}
