/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

namespace goat {

    class object;
    class gc_data;

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

    /**
     * @brief Creates object containing real number
     * @param gc Data required for the garbage collector
     * @param value The value
     * @return Object containing real number
     */
    object * create_real_number(gc_data* const gc, const double value);
}
