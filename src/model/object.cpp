/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "object.h"

namespace goat {
    
    bool object::less(const object* const other) const {
        /*
           In general, an object is less than another if its address in memory is less.
        */
        return this < other;
    }

    std::wstring object::to_string() const {
        return to_string_notation();
    }

    bool object::get_string_value(std::wstring* const value_ptr) const {
        return false;
    }

    void object::set_child_unsafe(object *key, variable &value) {
        auto pair = children.find(key);
        if (pair != children.end()) {
            if (pair->second.obj != value.obj) {
                pair->second.obj->release();
                pair->second = value;
                pair->second.obj->add_reference();
            } else {
                pair->second.data = value.data;
            }
        } else {
            children[key] = value;
            key->add_reference();
            value.add_reference();
        }
    }
}
