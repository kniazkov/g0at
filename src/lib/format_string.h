/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <string>

namespace goat {

    /**
     * @brief Formats the string, replaces all inclusions of the substring <code>{0}</code>
     *   with the argument 0
     * @param format The string to be formatted
     * @param arg0 The argument by which the substring <code>{0}</code> is replaced
     * @return Formatted string
     */
    std::wstring format_string(std::wstring format, std::wstring arg0);

    /**
     * @brief Formats the string, replaces all inclusions of the substring <code>{n}</code>
     *   where n = 0..1 with the arguments
     * @param format The string to be formatted
     * @param arg0 The argument by which the substring <code>{0}</code> is replaced
     * @param arg1 The argument by which the substring <code>{1}</code> is replaced
     * @return Formatted string
     */
    std::wstring format_string(std::wstring format, std::wstring arg0, std::wstring arg1);
}
