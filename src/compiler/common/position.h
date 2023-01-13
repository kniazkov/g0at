/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

namespace goat {

    /**
     * @brief Defines the position of something in the source code
     */
    struct position {
        /**
         * @brief Source code file name
         */
        const char * file_name;

        /**
         * @brief Line number (starts from 1)
         */
        unsigned int line;

        /**
         * @brief Column number (starts from 1)
         */
        unsigned int column;

        /**
         * @brief Pointer to the source code
         */
        const wchar_t * code;

        /**
         * @brief The length of this entity (in characters), always non-zero
         */
        unsigned int length;
    };
}
