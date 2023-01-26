/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <exception>

namespace goat {

    class object;

    /**
     * @brief Descriptor containing Goat exception data
     */
    class runtime_exception_data {
        friend class runtime_exception;

    public:
        /**
         * @brief Constructor
         * @param obj Object thrown as an exception
         */
        runtime_exception_data(object *obj);

        /**
         * @brief Destructor
         */
        ~runtime_exception_data();

    private:
        /**
         * @brief Object thrown as an exception
         */
        object *obj;

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
        runtime_exception_data(const runtime_exception_data&) {
        }

        /**
         * @brief Private assigment operator to prevent copying
         */
        void operator=(const runtime_exception_data&) {
        }
    };

    /**
     * @brief A wrapper that allows a Goat exception object to be presented as a C++ exception
     */
    class runtime_exception : public std::exception {
    public:
        /**
         * @brief Constructor
         * @param obj Object thrown as an exception
         */
        runtime_exception(object *obj);

        /**
         * @brief Copy constructor
         * @param other Other wrapper
         */
        runtime_exception(const runtime_exception &other);

        /**
         * @brief Assignment operator
         * @param other Other wrapper
         */
        runtime_exception& operator=(const runtime_exception &other);

        /**
         * @brief Destructor
         */
        ~runtime_exception();

        /**
         * @brief Returns the explanatory string
         * @return Pointer to a null-terminated string with explanatory information
         */
        const char* what() const noexcept override;

    private:
        /**
         * @brief Descriptor containing Goat exception data
         */
        runtime_exception_data *data;
    };

    /**
     * @return Pointer to the prototype of exception objects
     */
    object * get_exception_prototype();

    /**
     * @return Pointer to the "illegal argument" exception instance
     */
    object * get_illegal_agrument_exception();

    /**
     * @return Pointer to the "operation not supported" exception instance
     */
    object * get_operation_not_supported_exception();
}
