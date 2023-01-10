/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "token.h"

namespace goat {

    /**
     * @brief The scanner splits the source code into a set of tokens
     * 
     * The scanner works sequentially and generates tokens one at a time.
     */
    class scanner {
    public:
        /**
         * @brief Returns the next token
         * @return The next token or <code>nullptr</code> if the sequence contains no more tokens
         */
        token * get_token();        
    };
}
