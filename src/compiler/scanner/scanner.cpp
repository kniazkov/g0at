/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <sstream>
#include "scanner.h"

namespace goat {

    /**
     * @brief Determines whether the symbol is a space
     */
    static inline bool is_space(wchar_t c) {
        return c == ' ' || c == '\r' || c == '\n' || c == '\t';
    }

    /**
     * @brief Determines whether the symbol is a letter
     */
    static inline bool is_letter(wchar_t c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c > 127;
    }

    /**
     * @brief Determines whether the symbol is digit
     */
    static inline bool is_digit(wchar_t c) {
        return c >= '0' && c <= '9';
    }

    scanner::scanner(std::vector<token*> *tokens, const char *file_name,
            std::wstring &code) : tokens(tokens) {
        b.type = token_type::unknown;
        b.file_name = file_name;
        b.line = 1;
        b.column = 1;
        b.code = code.c_str();
        b.length = 0;

        code_end = b.code + code.size();
    }

    token * scanner::get_token() {
        wchar_t c = get_char();

        while (is_space(c)) {
            c = next_char();
        }

        if (c == 0) {
            return nullptr;
        }

        if (is_letter(c)) {
            token *t = new token(b);
            do {
                t->length++;
                c = next_char();                
            } while(is_letter(c) || is_digit(c));
            t->type = token_type::identifier;
            tokens->push_back(t);
            return t;
        }

        if (c == L'\"') {
            token_string *t = new token_string(b);
            c = next_char();
            std::wstringstream s;
            while (c != '\"') {
                s << c;
                t->length++;
                c = next_char();
            }
            c = next_char();
            t->data = s.str();
            tokens->push_back(t);
            return t;
        }

        switch(c) {
            case '(': {
                token_bracket *t = new token_bracket(b, token_type::opening_bracket, ')');
                next_char();
                tokens->push_back(t);
                return t;
            }
            case '{': {
                token_bracket *t = new token_bracket(b, token_type::opening_bracket, '}');
                next_char();
                tokens->push_back(t);
                return t;
            }
            case '[': {
                token_bracket *t = new token_bracket(b, token_type::opening_bracket, ']');
                tokens->push_back(t);
                return t;
            }
            case ')': {
                token_bracket *t = new token_bracket(b, token_type::closing_bracket, '(');
                next_char();
                tokens->push_back(t);
                return t;
            }
            case '}': {
                token_bracket *t = new token_bracket(b, token_type::closing_bracket, '{');
                next_char();
                tokens->push_back(t);
                return t;
            }
            case ']': {
                token_bracket *t = new token_bracket(b, token_type::closing_bracket, '[');
                next_char();
                tokens->push_back(t);
                return t;
            }
            case ';': {
                token *t = new token(b);
                t->type = token_type::semicolon;
                t->length = 1;
                tokens->push_back(t);
                return t;
                next_char();
            }
        }

        return nullptr;
    }

    char scanner::get_char() {
        return b.code < code_end ? *(b.code) : 0;
    }

    char scanner::next_char() {
        if (b.code < code_end) {
            if (*(b.code) == '\n') {
                b.column = 0;
                b.line++;
            }
            b.code++;
            if (b.code < code_end) {
                b.column++;
                return *(b.code);
            }
        }
        return 0;
    }
}
