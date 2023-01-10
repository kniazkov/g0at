/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "static_object.h"

namespace goat {

    void static_object::add_reference() {
        // Since a static object is never deleted, reference counting is not needed for it
    }

    void static_object::release() {
        // Since a static object is never deleted, reference counting is not needed for it
    }

    gc_data * static_object::get_garbage_collector_data() const {
        return nullptr;
    }

    void static_object::set_attribute(object *key, variable &value) {
        /**
         * @todo: Exception
         */
    }
}
