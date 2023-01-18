/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "token_iterator.h"

namespace goat {

    class statement;

    /**
     * @brief Tries to parse the list of tokens as a statement
     * @param iter Iterator by token
     * @return A statement or <code>nullptr</code> if there are no more tokens
     */
    statement * parse_statement(token_iterator *iter);
}
