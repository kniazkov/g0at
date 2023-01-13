/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "test_list.h"
#include "test_model.h"
#include "test_scanner.h"

namespace goat {

    static test_description test_list[] = {
        { "empty object", test_empty_object },
        { "dynamic string", test_dynamic_string },
        { "string notation of a generic object", test_generic_object_string_notation },
        { "static strings declaration", test_static_string_declaration },
        { "'is_instance_of' method", test_is_instance_of },
        { "'get_attribute' method, single inheritance", test_get_attribute },
        { "'get_attribute' method, multiple inheritance", test_get_attribute_multiple_inheritance },
        { "multiple threads create and delete objects", test_multithreaded_object_creation },
        { "square root calculation", test_square_root },
        { "root scope", test_root_scope },
        { "main scope", test_main_scope },
        { "'read_variable' expression", test_read_variable_expression },
        { "'function_call' expression", test_function_call_expression },
        { "'print' function", test_function_print },
        { "string concatenation", test_string_concatenation },
        { "'sqrt' function with illegal arguments", test_sqrt_function_with_illegal_argument },
        { "sum two real numbers", test_sum_two_real_numbers },
        { "sum two real numbers with exception", test_sum_two_real_numbers_with_exception },

        { "scanner: one identifier", test_scan_identifier },
        { "scanner: new line (rows and columns counting)", test_scan_new_line },
        { "scanner: 'hello world' program", test_scan_hello_world },
        { "scanner: not-ASCII (cyrillic) identifier", test_scan_cyrillic_identifier }
    };

    unsigned int get_number_of_tests() {
        return sizeof(test_list) / sizeof(test_description);
    }

    const test_description * get_tests() {
        return test_list;
    }
}
