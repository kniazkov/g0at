/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <string>

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
         * @brief Offset from start of file
         */
        unsigned int offset;

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
         * @brief The length of this entity (in characters)
         */
        unsigned int length;

        /**
         * @brief Returns the current line of code as text
         * @return Source code
         */
        std::wstring get_current_line_as_text() const;

        /**
         * @brief Merges two positions, so that the new position covers two elements
         * @param other Another position
         * @return Merged position
         */
        position merge_position(const position *other) const;
    };
}
