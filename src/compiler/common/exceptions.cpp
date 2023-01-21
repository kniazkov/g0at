/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <sstream>
#include "exceptions.h"
#include "lib/utf8_encoder.h"

namespace goat {

    compiler_exception::compiler_exception(position *pos, std::wstring message) {
        std::wstringstream stream;
        stream << pos->file_name << ", " << pos->line << '.' << pos->column << ": " <<
            message;
        buff = encode_utf8(stream.str());
    }

    const char* compiler_exception::what() const noexcept {
        return buff.c_str();
    }
}
