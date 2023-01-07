/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "static_object.h"
#include "dynamic_object.h"

namespace goat {
    /**
     * @brief Object that stores an unicode string
     */
    class base_string : public virtual object {
    private:
        /**
         * @brief The value
         */
        std::wstring value;

    public:
        /**
         * @brief Constructor
         * @param value The value
         */
        base_string(const std::wstring &value);

        object_type get_type() const override;
        bool less(const object* const others) const override;
        object *get_first_prototype() const override;
        std::wstring to_string(const variable* var) const override;
        std::wstring to_string_notation(const variable* var) const override;
        bool get_string_value(std::wstring* const value_ptr) const override;
        variable do_addition(gc_data* const gc,
            const variable* left, const variable* right) const override;
    };

    /**
     * @brief A static string, that is, one that always exists in the system
     *   and is not considered by the garbage collector
     */
    class static_string : public static_object, public base_string {
    public:
        /**
         * @brief Constructor
         * @param value The value
         */
        static_string(const std::wstring &value) : 
            base_string(value) {
        }
    };

    /**
     * @brief A dynamic string, that is, one that is created while the program is running
     */
    class dynamic_string : public dynamic_object, public base_string {
    public:
        /**
         * @brief Constructor
         * @param gc Data required for the garbage collector
         * @param value The value
         */
        dynamic_string(gc_data* const gc, const std::wstring &value) : 
                dynamic_object(gc), base_string(value) {
        }
    };

    /**
     * @return Pointer to the prototype of string objects
     */
    object * get_string_prototype();
}
