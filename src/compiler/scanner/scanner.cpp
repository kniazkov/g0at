/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <sstream>
#include <cstring>
#include <map>
#include "scanner.h"
#include "compiler/common/exceptions.h"
#include "resources/messages.h"

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

    /**
     * @brief Determines whether a symbol belongs to an operator
     */
    static inline bool is_operator(wchar_t c) {
        switch(c) {
            case '+':
            case '-':
            case '*':
            case '\\':
            case '>':
            case '<':
            case '=':
                return true;
            default:
                return false;
        }
    }

    /**
     * The list of single-character tokens
     */
    std::map<wchar_t, token_type> single_character_tokens = {
        { '.', token_type::dot },
        { ',', token_type::comma },
        { ';', token_type::semicolon },
        { '$', token_type::dollar_sign }
    };

    /**
     * The structure that describes the bracket
     */
    struct bracket_descriptor {
        /**
         * The token type
         */
        token_type type;

        /**
         * @brief Paired bracket to this bracket (to check that the brackets are correct)
         */
        char paired_bracket;        
    };

    /**
     * The list of tokens containing brackets
     */
    std::map<wchar_t, bracket_descriptor> brackets_list = {
        { '(', { token_type::opening_bracket, ')' } },
        { '{', { token_type::opening_bracket, '}' } },
        { '[', { token_type::opening_bracket, ']' } },
        { ')', { token_type::closing_bracket, '(' } },
        { '}', { token_type::closing_bracket, '{' } },
        { ']', { token_type::closing_bracket, '[' } }
    };    

    scanner::scanner(std::vector<token*> *tokens, const char *file_name,
            std::wstring &code) : tokens(tokens) {
        b.type = token_type::unknown;
        b.file_name = file_name;
        b.offset = 0;
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
            token *t = new token(b);
            t->type = token_type::end;
            tokens->push_back(t);
            return t;
        }

        if (is_letter(c)) {
            token *t = new token(b);
            do {
                t->length++;
                c = next_char();                
            } while(is_letter(c) || is_digit(c));
            if (t->length == 3 && 0 == std::memcmp(t->code, L"var", 3 * sizeof(wchar_t)))
                t->type = token_type::keyword_var;
            else if (t->length == 8 && 0 == std::memcmp(t->code, L"function", 8 * sizeof(wchar_t)))
                t->type = token_type::keyword_function;
            else if (t->length == 6 && 0 == std::memcmp(t->code, L"system", 6 * sizeof(wchar_t)))
                t->type = token_type::keyword_system;
            else
                t->type = token_type::identifier;
            tokens->push_back(t);
            return t;
        }

        if (is_digit(c)) {
            int64_t int_part = 0;
            token_number *t = new token_number(b);
            do {
                int_part = int_part * 10 + c - '0';
                t->length++;
                c = next_char();
            } while(is_digit(c));
            t->data.int_value = int_part;
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

        if (is_operator(c)) {
            token *t = new token(b);
            do {
                t->length++;
                c = next_char();                
            } while(is_operator(c));
            t->type = token_type::operato;
            tokens->push_back(t);
            return t;
        }

        switch(c) {
            case '(':
            case '{':
            case '[':
            case ')':
            case '}':
            case ']':
            {
                bracket_descriptor descriptor = brackets_list[c];
                token_bracket *t = new token_bracket(b, descriptor.type, descriptor.paired_bracket);
                next_char();
                tokens->push_back(t);
                return t;
            }
            case '.':
            case ',':
            case ';':
            case '$': {
                token *t = new token(b);
                next_char();
                t->type = single_character_tokens[c];
                t->length = 1;
                tokens->push_back(t);
                return t;
            }
        }

        std::wstringstream error;
        error << get_messages()->msg_unknown_symbol() << ": '" << c << '\'';
        throw compiler_exception(new compiler_exception_data(&b, error.str()));
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
            b.offset++;
            b.code++;
            if (b.code < code_end) {
                b.column++;
                return *(b.code);
            }
        }
        return 0;
    }
}
