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
    class goat_exception_data {
        friend class goat_exception_wrapper;

    public:
        /**
         * @brief Constructor
         * @param obj Object thrown as an exception
         */
        goat_exception_data(object *obj);

        /**
         * @brief Destructor
         */
        ~goat_exception_data();

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
        goat_exception_data(const goat_exception_data&) {
        }

        /**
         * @brief Private assigment operator to prevent copying
         */
        void operator=(const goat_exception_data&) {
        }
    };

    /**
     * @brief A wrapper that allows a Goat exception object to be presented as a C++ exception
     */
    class goat_exception_wrapper : public std::exception {
    public:
        /**
         * @brief Constructor
         * @param obj Object thrown as an exception
         */
        goat_exception_wrapper(object *obj);

        /**
         * @brief Copy constructor
         * @param other Other wrapper
         */
        goat_exception_wrapper(const goat_exception_wrapper &other);

        /**
         * @brief Assignment operator
         * @param other Other wrapper
         */
        goat_exception_wrapper& operator=(const goat_exception_wrapper &other);

        /**
         * @brief Destructor
         */
        ~goat_exception_wrapper();

        /**
         * @brief Returns the explanatory string
         * @return Pointer to a null-terminated string with explanatory information
         */
        const char* what() const noexcept override;

    private:
        /**
         * @brief Descriptor containing Goat exception data
         */
        goat_exception_data *data;
    };

    /**
     * @return Pointer to the prototype of exception objects
     */
    object * get_exception_prototype();
}
