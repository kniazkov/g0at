/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <vector>
#include "token_iterator.h"

namespace goat {

    class object;
    class gc_data;
    class statement;

    /**
     * @brief Data needed for parsing
     */
    struct parser_data {
        /**
         * @brief List of objects that are created during parsing (needed to mark dynamic objects)
         */
        std::vector<object*> *objects;

        /**
         * @brief Data required for the garbage collector (needed to create dynamic objects)
         */
        gc_data *gc;
    };

    /**
     * @brief Tries to parse the list of tokens as a statement
     * @param data Data needed for parsing
     * @param iter Iterator by token
     * @return A statement or <code>nullptr</code> if there are no more tokens
     */
    statement * parse_statement(parser_data *data, token_iterator *iter);
}
