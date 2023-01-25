/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "position.h"

namespace goat {

    std::wstring position::get_current_line_as_text() const {
        const wchar_t *begin = code;
        unsigned int size = 0,
            index = offset;
        while(*begin != '\n' && index > 0) {
            begin--;
            size++;
            index--;
        }
        if (*begin == '\n') {
            begin++;
        }
        const wchar_t *end = code + 1;
        while (*end != '\r' && *end != '\n' && *end != 0) {
            end++;
            size++;
        }
        return std::wstring(begin, size);
    }
}
