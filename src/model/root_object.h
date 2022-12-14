/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "generic_object.h"

namespace goat {

    /**
     * @brief The root object in the inheritance hierarchy
     * 
     * Any other object has this object as a prototype.
     */
    class root_object : public generic_static_object {
    public:
        /**
         * Constructor
         */
        root_object();
    };

    /**
     * @return Pointer to the root object instance
     */
    object *get_root_object();

    /**
     * @return Pointer to prototype list that contains root object
     */
    prototype_list * get_prototype_list_contains_root_object();
}
