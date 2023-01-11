/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "scanner.h"

namespace goat {

    scanner::scanner(std::vector<token*> & tokens, const char *file_name,
            const char *code, unsigned int length) : tokens(tokens) {
        b.type = token_type::unknown;
        b.file_name = file_name;
        b.line = 1;
        b.column = 1;
        b.code = code;
        b.length = 0;

        code_end = code + length;
    }

    token * scanner::get_token() {
        return nullptr;
    }
}
