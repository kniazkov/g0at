/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "object.h"

namespace goat {

    bool test_empty_object() {
        return get_empty_object()->to_string() == L"{}";
    }

}