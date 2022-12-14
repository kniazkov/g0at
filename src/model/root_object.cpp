/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "root_object.h"

namespace goat {

    root_object::root_object() {
        /*
         * The root object itself has no prototype:
         */
        proto.count = 0;
        proto.data.obj = nullptr;
    }

    static root_object instance;
    static object *ptr = &instance;
    static prototype_list list = { &ptr, 1 };

    object * get_root_object() {
        return &instance;
    }

    prototype_list * get_prototype_list_contains_root_object() {
        return &list;
    }
}
