/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cstring>
#include "unit_testing.h"
#include "compiler/common/exceptions.h"
#include "compiler/scanner/scanner.h"
#include "compiler/scanner/brackets_processor.h"

namespace goat {

    bool test_scan_identifier() {
        std::vector<token*> tokens;
        std::wstring code = L"abc";
        scanner scan(&tokens, "test", code);
        token *tok = scan.get_token();
        assert_equals(bool, true, token_type::identifier == tok->type);
        assert_equals(int, 0, std::strcmp("test", tok->file_name));
        assert_equals(unsigned int, 1, tok->line);
        assert_equals(unsigned int, 1, tok->column);
        assert_equals(int, 0, std::memcmp(L"abc", tok->code, 3 * sizeof(wchar_t)));
        assert_equals(unsigned int, 3, tok->length);
        for (token *tok : tokens) {
            delete tok;
        }
        return true;
    }

    bool test_scan_new_line() {
        std::vector<token*> tokens;
        std::wstring code = L"aaa\r\n  bbbb";
        scanner scan(&tokens, nullptr, code);
        token *tok = scan.get_token();
        tok = scan.get_token();
        assert_equals(bool, true, token_type::identifier == tok->type);
        assert_equals(unsigned int, 2, tok->line);
        assert_equals(unsigned int, 3, tok->column);
        assert_equals(int, 0, std::memcmp(L"bbbb", tok->code, tok->length * sizeof(wchar_t)));
        for (token *tok : tokens) {
            delete tok;
        }
        return true;
    }

    bool test_scan_hello_world() {
        std::vector<token*> tokens;
        std::wstring code = L"\n  print(\"hello world\");";
        scanner scan(&tokens, nullptr, code);
        token *t1 = scan.get_token();
        assert_equals(bool, true, token_type::identifier == t1->type);
        assert_equals(unsigned int, 2, t1->line);
        assert_equals(unsigned int, 3, t1->column);
        assert_equals(int, 0, std::memcmp(L"print", t1->code, t1->length * sizeof(wchar_t)));
        token *t2 = scan.get_token();
        assert_equals(bool, true, token_type::opening_bracket == t2->type);
        assert_equals(unsigned int, 2, t2->line);
        assert_equals(unsigned int, 8, t2->column);
        assert_equals(unsigned int, 1, t2->length);
        token_bracket *t2_b = (token_bracket*)t2;
        assert_equals(char, '(', t2_b->bracket);
        assert_equals(char, ')', t2_b->paired_bracket);
        token *t3 = scan.get_token();
        assert_equals(bool, true, token_type::string == t3->type);
        assert_equals(unsigned int, 2, t3->line);
        assert_equals(unsigned int, 9, t3->column);
        assert_equals(unsigned int, 13, t3->length);
        token_string *t3_s = (token_string*)t3;
        assert_equals(std::wstring, L"hello world", t3_s->data);
        token *t4 = scan.get_token();
        assert_equals(bool, true, token_type::closing_bracket == t4->type);
        assert_equals(unsigned int, 2, t4->line);
        assert_equals(unsigned int, 22, t4->column);
        assert_equals(unsigned int, 1, t4->length);
        token_bracket *t4_b = (token_bracket*)t4;
        assert_equals(char, ')', t4_b->bracket);
        assert_equals(char, '(', t4_b->paired_bracket);
        token *t5 = scan.get_token();
        assert_equals(bool, true, token_type::semicolon == t5->type);
        assert_equals(unsigned int, 2, t5->line);
        assert_equals(unsigned int, 23, t5->column);
        assert_equals(unsigned int, 1, t5->length);
        for (token *tok : tokens) {
            delete tok;
        }
        return true;
    }

    bool test_scan_cyrillic_identifier() {
        std::vector<token*> tokens;
        std::wstring code = L"    счётчик  ";
        scanner scan(&tokens, nullptr, code);
        token *tok = scan.get_token();
        assert_equals(bool, true, token_type::identifier == tok->type);
        assert_equals(unsigned int, 1, tok->line);
        assert_equals(unsigned int, 5, tok->column);
        assert_equals(int, 0, std::memcmp(L"счётчик", tok->code, tok->length * sizeof(wchar_t)));
        for (token *tok : tokens) {
            delete tok;
        }
        return true;
    }

    bool test_unknown_symbol_exception() {
        std::vector<token*> tokens;
        std::wstring code = L"abc`";
        scanner scan(&tokens, "test.goat", code);
        bool oops = false;
        try {
            scan.get_token();
            scan.get_token();
        }
        catch (compiler_exception ex) {
            oops = true;
            int result = std::strcmp(
                "test.goat, 1.4: Unknown symbol: '`'",
                ex.what()
            );
            assert_equals(int, 0, result);
        }
        assert_equals(bool, true, oops);
        for (token *tok : tokens) {
            delete tok;
        }
        return true;
    }

    bool test_scan_and_process_hello_world() {
        std::vector<token*> tokens,
            result;
        std::wstring code = L"print( \"hello world\" );";
        scanner scan(&tokens, nullptr, code);
        process_brackets(&scan, &tokens, &result);
        assert_equals(size_t, 3, result.size());
        assert_equals(bool, true, token_type::identifier == result[0]->type);
        assert_equals(unsigned int, 1, result[0]->line);
        assert_equals(unsigned int, 1, result[0]->column);
        assert_equals(int, 0, std::memcmp(L"print", result[0]->code, result[0]->length * sizeof(wchar_t)));
        assert_equals(bool, true, token_type::brackets_pair == result[1]->type);
        assert_equals(unsigned int, 1, result[1]->line);
        assert_equals(unsigned int, 6, result[1]->column);
        assert_equals(unsigned int, 17, result[1]->length);
        token_brackets_pair *pair = (token_brackets_pair*)result[1];
        assert_equals(char, '(', pair->opening_bracket);
        assert_equals(size_t, 1, pair->tokens.size());
        assert_equals(bool, true, token_type::string == pair->tokens[0]->type);
        assert_equals(bool, true, token_type::semicolon == result[2]->type);
        assert_equals(unsigned int, 1, result[2]->line);
        assert_equals(unsigned int, 23, result[2]->column);
        assert_equals(unsigned int, 1, result[2]->length);
        for (token *tok : tokens) {
            delete tok;
        }
        return true;
    }

    bool test_opening_bracket_without_closing() {
        std::vector<token*> tokens,
            result;
        std::wstring code = L"print(\"oops\" ;";
        scanner scan(&tokens, "test.goat", code);
        bool oops = false;
        try {
            process_brackets(&scan, &tokens, &result);
        }
        catch(compiler_exception ex) {
            oops = true;
            int result = std::strcmp(
                "test.goat, 1.6: The bracket '(' was not closed",
                ex.what()
            );
            assert_equals(int, 0, result);
        }
        assert_equals(bool, true, oops);
        for (token *tok : tokens) {
            delete tok;
        }
        return true;
    }

    bool test_closing_bracket_without_opening() {
        std::vector<token*> tokens,
            result;
        std::wstring code = L"print \"oops\");";
        scanner scan(&tokens, "test.goat", code);
        bool oops = false;
        try {
            process_brackets(&scan, &tokens, &result);
        }
        catch(compiler_exception ex) {
            oops = true;
            int result = std::strcmp(
                "test.goat, 1.13: The closing bracket ')' without opening one",
                ex.what()
            );
            assert_equals(int, 0, result);
        }
        assert_equals(bool, true, oops);
        for (token *tok : tokens) {
            delete tok;
        }
        return true;
    }

    bool test_brackets_do_not_match() {
        std::vector<token*> tokens,
            result;
        std::wstring code = L"print(\"oops\"};";
        scanner scan(&tokens, "test.goat", code);
        bool oops = false;
        try {
            process_brackets(&scan, &tokens, &result);
        }
        catch(compiler_exception ex) {
            oops = true;
            int result = std::strcmp(
                "test.goat, 1.13: The closing bracket '}' does not match the opening bracket '('",
                ex.what()
            );
            assert_equals(int, 0, result);
        }
        assert_equals(bool, true, oops);
        for (token *tok : tokens) {
            delete tok;
        }
        return true;
    }
}
