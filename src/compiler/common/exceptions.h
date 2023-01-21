/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <exception>
#include <string>
#include "position.h"

namespace goat {

    /**
     * @brief Exception thrown by the compiler in the case of a compilation error
     */
    class compiler_exception : public std::exception {
    public:
        /**
         * @brief Constructor
         * @param pos The position where the compilation error occurred
         * @param message Additional message
         */
        compiler_exception(position *pos, std::wstring message);
        /**
         * @brief Returns the explanatory string
         * @return Pointer to a null-terminated string with explanatory information
         */
        const char* what() const noexcept override;

    private:
        /**
         * @brief Buffer to store the explanatory string
         */
        std::string buff;
    };
}
