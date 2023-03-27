/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <string>
#include <vector>
#include "compiler/common/position.h"

namespace goat {

    /**
     * @brief All possible token types
     */
    enum class token_type {
        unknown = 0,
        identifier,
        string,
        integer,
        real,
        opening_bracket,
        closing_bracket,
        dot,
        comma,
        semicolon,
        dollar_sign,
        operato,
        keyword_var,
        keyword_function,
        end,
        brackets_pair
    };

    /**
     * @brief A token, that is, a structure that is generated by the scanner
     *   and defines some piece of source code
     */
    struct token : public position {
        /**
         * @brief Virtual destructor for inheritors
         */
        virtual ~token() {
        }

        /**
         * @brief The type of the token
         */
        token_type type;

        /**
         * @brief Sets some token values from the base token
         * @param base Base token
         */
        void set(token &base) {
            file_name = base.file_name;
            offset = base.offset;
            line = base.line;
            column = base.column;
            code = base.code;
            length = 0;
        }
    };

    /**
     * @brief Token representing a bracket
     */
    struct token_bracket : public token {
        /**
         * @brief Constructor
         * @param base Base token
         * @param type Type of token
         * @param paired_bracket Paired bracket to this bracket
         */
        token_bracket(token &base, token_type type, char paired_bracket) {
            set(base);
            this->type = type;
            this->length = 1;
            this->bracket = (char)*code;
            this->paired_bracket = paired_bracket;
        }

        /**
         * @brief This bracket
         */
        char bracket;

        /**
         * @brief Paired bracket to this bracket (to check that the brackets are correct)
         */
        char paired_bracket;
    };

    /**
     * @brief A token represents a pair of brackets, as well as all tokens within those brackets
     */
    struct token_brackets_pair : public token {
        /**
         * @brief Constructor
         * @param base Token describing opening bracket
         */
        token_brackets_pair(token_bracket *base) {
            set(*base);
            type = token_type::brackets_pair;
            opening_bracket = base->bracket;
            length = 2;
        }

        /**
         * @brief Adds information from the closing bracket
         * @param closing Token describing closing bracket
         */
        void set_closing_bracket(token_bracket *closing) {
            length = closing->offset - offset + 1;
        }

        /**
         * @brief Opening bracket
         */
        char opening_bracket;

        /**
         * @brief List of tokens within brackets
         */
        std::vector<token*> tokens;
    };

    /**
     * @brief Token representing string in quotes
     */
    struct token_string : public token {
        /**
         * @brief Constructor
         * @param base Base token
         */
        token_string(token &base) {
            set(base);
            type = token_type::string;
            length = 2; // token size - minimum 2 characters (2 quotes) 
        }

        /**
         * @brief String data
         */
        std::wstring data;
    };

    /**
     * @brief Token representing a number
     */
    struct token_number : public token {
        /**
         * @brief Constructor
         * @param base Base token
         */
        token_number(token &base) {
            set(base);
            this->type = token_type::integer;
        }

        /**
         * @brief Numeric data
         */
        union {
            /**
             * @brief Integer value
             */
            int64_t int_value;

            /**
             * @brief Real value
             */
            double real_value;
        } data;
    };
};
