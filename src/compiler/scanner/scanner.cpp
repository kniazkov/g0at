/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "scanner.h"

namespace goat {

    /**
     * @brief Determines whether the symbol is a space
     */
    static inline bool is_space(char c) {
        return c == ' ' || c == '\r' || c == '\n' || c == '\t';
    }

    /**
     * @brief Determines whether the symbol is a letter
     */
    static inline bool is_letter(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    }

    /**
     * @brief Determines whether the symbol is digit
     */
    static inline bool is_digit(char c) {
        return c >= '0' && c <= '9';
    }

    scanner::scanner(std::vector<token*> *tokens, const char *file_name,
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
        char c = get_char();

        while (is_space(c)) {
            c = next_char();
        }

        if (c == 0) {
            return nullptr;
        }

        token *t = new token(b);
        tokens->push_back(t);

        if (is_letter(c)) {
            do {
                t->length++;
                c = next_char();                
            } while(is_letter(c) || is_digit(c));

            t->type = token_type::identifier;
            return t;
        }

        t->length = 1;
        switch(c) {
            case '(':
            case '[':
            case '{':
                next_char();
                t->type = token_type::opening_bracket;
                return t;
            case ')':
            case ']':
            case '}':
                next_char();
                t->type = token_type::closing_bracket;
                return t;
        }

        tokens->pop_back();
        delete t;
        return nullptr;
    }

    char scanner::get_char() {
        return b.code < code_end ? *(b.code) : 0;
    }

    char scanner::next_char() {
        if (b.code < code_end) {
            b.code++;
            if (b.code < code_end) {
                char c = *(b.code);
                if (c == '\n') {
                    b.column = 0;
                    b.line++;
                } else {
                    b.column++;
                }
                return c;
            }
        }
        return 0;
    }
}
