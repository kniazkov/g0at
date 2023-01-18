/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <vector>

namespace goat {

    class token;

    /**
     * @brief Iterator by token, that is, a structure that can give tokens one by one
     */
    class token_iterator {
    public:
        /**
         * @brief Returns the current token (which is at the beginning of a token list)
         *   without deleting it
         * 
         * That is, the next time you call this method, it will return the same token
         * 
         * @return A token
         */
        virtual token * get() const = 0;

        /**
         * @brief Returns the next token, which now becomes the beginning of a list
         * @return A token
         */
        virtual token * next() = 0;

        /**
         * @brief Checks iterator for validity.
         *   The iterator is invalid if there are no more tokens in it
         * @return Checking result
         */
        virtual bool valid() const = 0;
    };

    /**
     * @brief Iterator by token, vector wrapper
     */
    class token_iterator_over_vector : public token_iterator {
    public:
        /**
         * @brief Constructor
         * @param vector Vector containing tokens
         */
        token_iterator_over_vector(std::vector<token*> &vector) {
            ptr = vector.begin();
            end = vector.end();
        }

        token * get() const override {
            return ptr != end ? *ptr : nullptr;            
        }

        token * next() override {
            if (ptr != end) {
                ptr++;
                if (ptr != end) {
                    return *ptr;
                }
            }
            return nullptr;
        }

        bool valid() const override {
            return ptr != end;
        }

    private:
        /**
         * @brief Pointer to the current token
         */
        std::vector<token*>::iterator ptr;

        /**
         * @brief Pointer to the token after the last
         */
        std::vector<token*>::iterator end;
    };
}
