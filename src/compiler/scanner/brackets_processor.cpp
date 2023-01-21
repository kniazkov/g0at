/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "brackets_processor.h"
#include "scanner.h"
#include "compiler/common/exceptions.h"
#include "resources/messages.h"
#include "lib/format_string.h"

namespace goat {

    /**
     * @brief Combines tokens inside brackets into a special non-terminal token
     *   <code>brackets_pair</code>, recursive function
     * @param scan Scanner
     * @param all_tokens An array to store all tokens (to free up memory after parsing)
     * @param result List of tokens, which also contains this new type of token
     * @param prev_opening_bracket Token describing opening bracket
     * @return Last scanned token
     */
    static token_bracket * process_tokens(
            scanner *scan,
            std::vector<token*> *all_tokens, std::vector<token*> *result,
            token_bracket *prev_opening_bracket) {
        token *tok = scan->get_token();
        while (tok->type != token_type::end) {
            if (tok->type == token_type::opening_bracket) {
                token_bracket *opening_bracket = (token_bracket*)tok;
                token_brackets_pair *pair = new token_brackets_pair(opening_bracket);
                all_tokens->push_back(pair);
                result->push_back(pair);
                token_bracket *closing_bracket =
                    process_tokens(scan, all_tokens, &pair->tokens, opening_bracket);
                pair->set_closing_bracket(closing_bracket);
            }
            else if (tok->type == token_type::closing_bracket) {
                token_bracket *closing_bracket = (token_bracket*)tok;
                if (prev_opening_bracket) {
                    if (prev_opening_bracket->paired_bracket != closing_bracket->bracket) {
                        wchar_t br0str[] = {(wchar_t)closing_bracket->bracket, 0};
                        wchar_t br1str[] = {(wchar_t)prev_opening_bracket->bracket, 0};
                        std::wstring message = format_string(
                                get_messages()->msg_brackets_do_not_match(),
                                br0str,
                                br1str
                            );
                        throw compiler_exception(closing_bracket, message);
                    }
                }
                else {
                    wchar_t bracket[] = {(wchar_t)closing_bracket->bracket, 0};
                    std::wstring message = format_string(
                            get_messages()->msg_closing_bracket_without_opening(),
                            bracket
                        );
                    throw compiler_exception(closing_bracket, message);
                }
                return closing_bracket;
            }
            else {
                result->push_back(tok);
            }
            tok = scan->get_token();
        }
        if (prev_opening_bracket) {
            wchar_t bracket[] = {(wchar_t)prev_opening_bracket->bracket, 0};
            std::wstring message = format_string(
                    get_messages()->msg_not_closed_bracket(),
                    bracket
                );
            throw compiler_exception(prev_opening_bracket, message);
        }
        return nullptr;
    }


    void process_brackets(scanner *scan,
            std::vector<token*> *all_tokens, std::vector<token*> *result) {
        process_tokens(scan, all_tokens, result, nullptr);
    }
};