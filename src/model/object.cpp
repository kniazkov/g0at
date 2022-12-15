/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include "object.h"
#include "root_object.h"

namespace goat {
    
    object::object() {
    }

    object::~object() {
    }

    bool object::less(const object* const other) const {
        /*
           In general, an object is less than another if its address in memory is less:
        */
        return this < other;
    }

    object * object::get_prototype(unsigned int index) {
        return index ? nullptr : get_first_prototype();
    }

    object * object::get_first_prototype() {
        /*
            By default, the prototype for any object is the root object:
        */
        return get_root_object();
    }

    unsigned int object::get_number_of_prototypes() {
        /*
            By default, the object has one prototype:
        */
        return 1;
    }

    std::wstring object::to_string() const {
        return to_string_notation();
    }

    bool object::get_string_value(std::wstring* const value_ptr) const {
        return false;
    }
}
