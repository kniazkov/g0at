/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "root_object.h"

namespace goat {

    root_object::root_object() {

    }

    object * root_object::get_first_prototype() const {
        /*
            Root object has no prototypes:
        */
        return nullptr;
    }

    unsigned int root_object::get_number_of_prototypes() const {
        /*
            Root object has no prototypes:
        */
        return 0;
    }

    bool root_object::is_instance_of(const object* proto) const {
        return this == proto;
    }

    variable * root_object::get_attribute(object *key) {
        return get_own_attribute(key);
    }

    static root_object instance;

    object * get_root_object() {
        return &instance;
    }
}
