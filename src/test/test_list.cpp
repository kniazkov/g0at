/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "test_list.h"
#include "test_model.h"

namespace goat {

    static test_description test_list[] = {
        { "empty object", test_empty_object },
        { "dynamic string", test_dynamic_string },
        { "string notation of a generic object", test_generic_object_string_notation },
        { "static strings declaration", test_static_string_declaration },
        { "'is_instance_of' method", test_is_instance_of },
        { "'get_attribute' method, single inheritance", test_get_attribute },
        { "'get_attribute' method, multiple inheritance", test_get_attribute_multiple_inheritance }
    };

    unsigned int get_number_of_tests() {
        return sizeof(test_list) / sizeof(test_description);
    }

    const test_description * get_tests() {
        return test_list;
    }
}
