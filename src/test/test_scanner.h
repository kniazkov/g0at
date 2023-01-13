/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

namespace goat {

    bool test_scan_identifier();
    bool test_scan_new_line();
    bool test_scan_hello_world();
    bool test_scan_cyrillic_identifier();
    bool test_unknown_symbol_exception();
}
