/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "utf8_encoder.h"
#include <cstdint>
#include <sstream>

namespace goat {
    
    static int encode_utf8_char(wchar_t w, char *c) {
        if (w < 0x80) {
            c[0] = (char)w;
            return 1;
        }
        if (w < 0x800) {
            c[0] = (char)((w & 0x7C0) >> 6) + 0xC0;
            c[1] = (char)(w & 0x3F) + 0x80;
            return 2;
        }
        if (w < 0x10000) {
            c[0] = (char)((w & 0xF000) >> 12) + 0xE0;
            c[1] = (char)((w & 0xFC0) >> 6) + 0x80;
            c[2] = (char)(w & 0x3F) + 0x80;
            return 3;
        }
#if WCHAR_MAX > 0xFFFF
        if (w < 0x200000) {
            c[0] = (char)((w & 0x1C0000) >> 18) + 0xF0;
            c[1] = (char)((w & 0x3F000) >> 12) + 0x80;
            c[2] = (char)((w & 0xFC0) >> 6) + 0x80;
            c[3] = (char)(w & 0x3F) + 0x80;
            return 3;
        }
#endif
        return 0;
    }

    std::string encode_utf8(std::wstring wstr)
    {
        std::stringstream ss;
        int i, n;
        char tmp[4];
        for (wchar_t w : wstr) {
            n = encode_utf8_char(w, tmp);
            if (n > 0) {
                for (i = 0; i < n; i++)
                    ss << tmp[i];
            }
            else
                ss << '?';
        }
        return ss.str();
    }

    std::wstring decode_utf8(std::string str) {
        size_t i = 0,
            len = str.size();
        std::wstringstream wss;
        while (i < len) {
            unsigned char c0 = (unsigned char)str[i];
            wchar_t w = 0;
            if ((c0 & 0x80) == 0) {
                w = c0;
                i++;
            }
            else if ((c0 & 0xE0) == 0xC0) {
                if (i + 1 >= len) {
                    throw bad_utf8();
                }
                unsigned char c1 = (unsigned char)str[i + 1];
                if ((c1 & 0xC0) != 0x80) {
                    throw bad_utf8();
                }
                w = ((c0 & 0x1F) << 6) + (c1 & 0x3F);
                i += 2;
            }
            else if ((c0 & 0xF0) == 0xE0) {
                if (i + 2 >= len) {
                    throw bad_utf8();
                }
                unsigned char c1 = (unsigned char)str[i + 1];
                unsigned char c2 = (unsigned char)str[i + 2];
                if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) {
                    throw bad_utf8();
                }
                w = ((c0 & 0xF) << 12) + ((c1 & 0x3F) << 6) + (c2 & 0x3F);
                i += 3;
            }
#if WCHAR_MAX > 0xFFFF
            else if ((c0 & 0xF8) == 0xF0) {
                if (i + 3 >= len) {
                    throw bad_utf8();
                }
                unsigned char c1 = (unsigned char)str[i + 1];
                unsigned char c2 = (unsigned char)str[i + 2];
                unsigned char c3 = (unsigned char)str[i + 3];
                if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
                    throw bad_utf8();
                }
                w = ((c0 & 0x7) << 18) + ((c1 & 0x3F) << 12) + ((c2 & 0x3F) << 6) + (c3 & 0x3F);
                i += 4;
            }
#endif
            else {
                throw bad_utf8();
            }
            wss << w;
        }
        return wss.str();
    };
};
