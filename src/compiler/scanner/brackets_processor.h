/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <vector>

namespace goat {

    class scanner;
    class token;

    /**
     * @brief Combines tokens inside brackets into a special non-terminal token
     *   <code>brackets_pair</code>
     * @param scan Scanner
     * @param all_tokens An array to store all tokens (to free up memory after parsing)
     * @param result List of tokens, which also contains this new type of token
     */
    void process_brackets(scanner *scan,
        std::vector<token*> *all_tokens, std::vector<token*> *result);

};