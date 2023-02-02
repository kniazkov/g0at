/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "cpp_type.h"
#include "model/strings.h"

namespace goat {

    static_string str_type_unknown(L"unknown");
    static_string str_type_invalid(L"invalid");
    static_string str_type_string(L"string");
    static_string str_type_integer(L"integer");
    static_string str_type_real(L"real");

    base_string * cpp_type_to_string_mapping[] = {
        &str_type_unknown,
        &str_type_invalid,
        &str_type_string,
        &str_type_integer,
        &str_type_real
    };

    base_string * cpp_type_to_string(cpp_type type) {
        return cpp_type_to_string_mapping[static_cast<int>(type)];
    }
}
