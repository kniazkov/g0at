/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "object_with_attributes.h"

namespace goat {

    variable * object_with_attributes::get_attribute(object *key) {
        variable *result = get_own_attribute(key);
        if (result != nullptr) {
            return result;
        }
        return get_first_prototype()->get_attribute(key);
    }

    variable * object_with_attributes::get_own_attribute(object *key) {
        auto pair = attributes.find(key);
        if (pair != attributes.end()) {
            return &pair->second;
        }
        return nullptr;
    }

    void object_with_attributes::set_attribute_unsafe(object *key, variable &value) {
        auto pair = attributes.find(key);
        if (pair != attributes.end()) {
            if (pair->second.obj != value.obj) {
                pair->second.obj->release();
                pair->second = value;
                pair->second.obj->add_reference();
            } else {
                pair->second.data = value.data;
            }
        } else {
            attributes[key] = value;
            key->add_reference();
            value.add_reference();
        }
    }
}
