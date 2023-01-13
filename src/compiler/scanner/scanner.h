/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <vector>
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
         * @brief Constructor
         * @param tokens An array to store all tokens (to free up memory after parsing)
         * @param file_name Source code file name
         * @param code Source code
         */
        scanner(std::vector<token*> *tokens, const char *file_name, std::wstring &code);

        /**
         * @brief Returns the next token
         * @return The next token or <code>nullptr</code> if the sequence contains no more tokens
         */
        token * get_token();

    private:
        /**
         * @brief An array to store all tokens (to free up memory after parsing)
         */
        std::vector<token*> * tokens;

        /**
         * @brief Token blank from which the resulting tokens are made
         */
        token b;

        /**
         * @brief Pointer to end of source code
         */
        const wchar_t * code_end;

        /**
         * @brief Returns the current character of the sequence
         * @brief A character or 0 if the sequence contains no more characters
         */
        char get_char();

        /**
         * @brief Returns the next character from the sequence
         * @brief A character or 0 if the sequence contains no more characters
         */
        char next_char();
    };
}
