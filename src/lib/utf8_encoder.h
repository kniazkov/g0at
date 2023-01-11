/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <string>

namespace goat
{
    /**
     * @brief Encodes the string in UTF-8 format
     * @param wstr Wide string (source)
     * @return Resulting string in UTF-8 format
     */
    std::string encode_utf8(std::wstring wstr);

    /**
     * @brief Decodes the string in UTF-8 format
     * @param str Source string
     * @return Decoded (wide) string
     */
    std::wstring decode_utf8(std::string str);
        
};