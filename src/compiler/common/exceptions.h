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
     * @brief Descriptor containing compiler exception data
     */
    class compiler_exception_data {
        friend class compiler_exception;

    public:
        /**
         * @brief Constructor
         * @param pos The position where the compilation error occurred
         * @param message Additional message
         */
        compiler_exception_data(position *pos, std::wstring message);

        /**
         * @brief Destructor
         */
        ~compiler_exception_data();

    private:
        /**
         * @brief The position where the compilation error occurred
         */
        position pos;

        /**
         * @brief Additional message
         */
        std::wstring message;

        /**
         * @brief Reference counter
         */
        unsigned int refs;

        /**
         * @brief Buffer containing explanatory string
         */
        char *buff;

        /**
         * @brief Private copy constructor to prevent copying
         */
        compiler_exception_data(const compiler_exception_data&) {
        }

        /**
         * @brief Private assigment operator to prevent copying
         */
        void operator=(const compiler_exception_data&) {
        }
    };

    /**
     * @brief Exception thrown by the compiler in the case of a compilation error
     */
    class compiler_exception : public std::exception {
    public:
        /**
         * @brief Constructor
         * @param data Descriptor containing compiler exception data
         */
        compiler_exception(compiler_exception_data *data);

        /**
         * @brief Copy constructor
         * @param other Other exception object
         */
        compiler_exception(const compiler_exception &other);

        /**
         * @brief Assignment operator
         * @param other Other exception object
         */
        compiler_exception& operator=(const compiler_exception &other);

        /**
         * @brief Destructor
         */
        ~compiler_exception();

        /**
         * @brief Returns the explanatory string
         * @return Pointer to a null-terminated string with explanatory information
         */
        const char* what() const noexcept override;

    private:
        /**
         * @brief Descriptor containing Goat exception data
         */
        compiler_exception_data *data;
    };
}
