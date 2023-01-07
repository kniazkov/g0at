/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "code.h"

namespace goat {

    void element::add_reference() {
        refs++;
    }

    void element::release() {
        if (!(--refs)) {
            delete this;
        }
    }

    gc_data * element::get_garbage_collector_data() const {
        return nullptr;
    }

    object_type element::get_type() const {
        return object_type::code;
    }

    /**
     * @todo: Notation for elements
     */
    std::wstring element::to_string_notation(const variable* var) const {
        return L"{}";
    }
    
    void element::set_attribute(object *key, variable &value) {
        /**
         * @todo: Exception
         */
    }
}
