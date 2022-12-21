/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "functions.h"

namespace goat {

    /**
     * @return Pointer to the function that calculates square root
     */
    base_function * get_sqrt_instance();
}