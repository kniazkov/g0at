/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cstring>
#include "unit_testing.h"
#include "compiler/scanner/scanner.h"

namespace goat {

    bool test_scan_identifier() {
        std::vector<token*> tokens;
        scanner scan(&tokens, "test", "abc", 3);
        token *tok = scan.get_token();
        assert_equals(bool, true, token_type::identifier == tok->type);
        assert_equals(int, 0, std::strcmp("test", tok->file_name));
        assert_equals(unsigned int, 1, tok->line);
        assert_equals(unsigned int, 1, tok->column);
        assert_equals(int, 0, std::strncmp("abc", tok->code, 3));
        assert_equals(unsigned int, 3, tok->length);
        for (token *tok : tokens) {
            delete tok;
        }
        return true;
    }

    bool test_scan_new_line() {
        std::vector<token*> tokens;
        const char *code = "aaa\r\n  bbbb";
        scanner scan(&tokens, nullptr, code, std::strlen(code));
        token *tok = scan.get_token();
        tok = scan.get_token();
        assert_equals(bool, true, token_type::identifier == tok->type);
        assert_equals(unsigned int, 2, tok->line);
        assert_equals(unsigned int, 3, tok->column);
        assert_equals(int, 0, std::strncmp("bbbb", tok->code, tok->length));
        for (token *tok : tokens) {
            delete tok;
        }
        return true;
    }
}
