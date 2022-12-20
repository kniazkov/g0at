/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "object.h"

namespace goat {
    
    /**
     * @brief An object that contains a set of attributes, that is, child objects
     */
    class object_with_attributes : public virtual object {
    public:
        variable * get_attribute(object *key) override;
        variable * get_own_attribute(object *key) override;
        
    protected:
        /**
         * @brief Comparator of two objects in order to place objects in the map
         * 
         * First it compares types, and then the contents of objects.
         */
        struct object_comparator {
            bool operator()(const object *first, const object* second) const {
                object_type first_type = first->get_type();
                object_type second_type = second->get_type();
                if (first_type == second_type) {
                    return first->less(second);
                }
                return first_type < second_type;
            }
        };

        /**
         * @brief Set of attributes (i.e. child objects)
         */
        std::map<object*, variable, object_comparator> attributes;
        
         /**
         * @brief Sets the attrubute (key-value pair), but does not check
         *   the correctness of this operation
         * @param key The key
         * @param value The value
         */
        void write_attribute(object *key, variable &value);
    };
}
