/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "position.h"

namespace goat {

    std::wstring position::get_current_line_as_text() const {
        const wchar_t *start = code;
        unsigned int size = 0,
            index = offset + 1;
        while(*start != '\n' && index > 0) {
            start--;
            size++;
            index--;
        }
        start++;
        const wchar_t *end = code + 1;
        while (*end != '\r' && *end != '\n' && *end != 0) {
            end++;
            size++;
        }
        return std::wstring(start, size);
    }
}
