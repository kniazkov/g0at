/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <string>

namespace goat {

    /**
     * @brief The basic essence of the Goat object-oriented language
     * 
     * Almost everything in the Goat programming language - data, functions, operators,
     * syntactic constructs, scopes, and so on - are objects. An object can contain other objects,
     * as well as be a prototype for other objects.
     */
    class object {
    public:
        
        /**
         * @brief Returns the string representation of the object,
         *   which is used for printing and debugging purposes
         * @return A string representation of an object 
         */
        virtual std::wstring to_string() = 0;
    };

    object * get_empty_object();
}
