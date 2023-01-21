/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <sstream>
#include "format_string.h"

namespace goat {

    /**
     * @brief Formats the string, replaces all inclusions of the substring <code>{n}</code>
     *   where n = 0..9 with the arguments
     * @param format The string to be formatted
     * @param argc Number of arguments 
     * @param arg1 Array containing arguments
     * @return Formatted string
     */
    static std::wstring format_string(std::wstring format, int argc, std::wstring* argv) {
        std::wstringstream stream;
        size_t i = 0,
            size = format.size();
        while (i < size) {
            if (format[i] == '{' && i + 2 < size &&
                    format[i + 1] >= '0' && format[i + 1] <= '9' && format[i + 2] == '}') {
                int index = format[i + 1] - '0';
                if (index < argc) {
                    stream << argv[index];
                }
                i += 2;
            }
            else {
                stream << format[i];
            }
            i++;
        }
        return stream.str();
    }

    std::wstring format_string(std::wstring format, std::wstring arg0) {
        std::wstring argv[1] = { arg0 };
        return format_string(format, 1, argv);
    }

    std::wstring format_string(std::wstring format, std::wstring arg0, std::wstring arg1) {
        std::wstring argv[2] = { arg0, arg1 };
        return format_string(format, 2, argv);
    }
}
