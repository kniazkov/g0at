/*
    Copyright 2023 Ivan Kniazkov

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
         * @brief Constructor
         */
        root_object();

        object *get_first_prototype() const override;
        unsigned int get_number_of_prototypes() const override;
        bool is_instance_of(const object *proto) const override;
        variable * get_attribute(object *key) override;
    };

    /**
     * @return Pointer to the root object instance
     */
    object *get_root_object();
}
