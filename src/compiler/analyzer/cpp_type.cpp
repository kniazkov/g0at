/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "cpp_type.h"

namespace goat {

    const char * cpp_type_to_string_mapping[] = {
        "unknown",
        "invalid",
        "string",
        "integer",
        "real"
    };

    const char * cpp_type_to_string(cpp_type type) {
        return cpp_type_to_string_mapping[static_cast<int>(type)];
    }
}
