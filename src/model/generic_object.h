/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "static_object.h"
#include "dynamic_object.h"

namespace goat {

    /**
     * @brief An "ordinary" object that does not have any special properties
     *   (such as built-in data types)
     */
    class generic_object : public virtual object {
    public:
        object_type get_type() const override;
        std::wstring to_string_notation() const override;
    };

    /**
     * @brief A predefined object permanently present in the system
     */
    class generic_static_object : public static_object, public generic_object {
    };

    /**
     * @brief List of prototypes used when creating a generic dynamic object
     */
    struct prototype_list {
        object** data;
        unsigned int count;
    };

    /**
     * @brief An object whose structure is set by the programmer in the program code
     */
    class generic_dynamic_object : public dynamic_object, public generic_object {
    public:
        /**
         * Constructor
         * @param gc Data required for the garbage collector
         * @param proto List of prototypes used when creating a dynamic object
         */
        generic_dynamic_object(gc_data *gc, prototype_list *proto);

        /**
         * Destructor
         */
        ~generic_dynamic_object();
    };
}
