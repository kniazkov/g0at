/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "test_list.h"
#include "test_model.h"
#include "test_scanner.h"
#include "test_parser.h"

namespace goat {

    static test_description test_list[] = {
        { "model: empty object", test_empty_object },
        { "model: dynamic string", test_dynamic_string },
        { "model: string notation of a generic object", test_generic_object_string_notation },
        { "model: static strings declaration", test_static_string_declaration },
        { "model: 'is_instance_of' method", test_is_instance_of },
        { "model: 'get_attribute' method, single inheritance", test_get_attribute },
        { "model: 'get_attribute' method, multiple inheritance", test_get_attribute_multiple_inheritance },
        { "model: multiple threads create and delete objects", test_multithreaded_object_creation },
        { "model: square root calculation", test_square_root },
        { "model: root scope", test_root_scope },
        { "model: main scope", test_main_scope },
        { "model: 'read_variable' expression", test_read_variable_expression },
        { "model: 'function_call' expression", test_function_call_expression },
        { "model: 'print' function", test_function_print },
        { "model: string concatenation", test_string_concatenation },
        { "model: 'sqrt' function with illegal arguments", test_sqrt_function_with_illegal_argument },
        { "model: sum two real numbers", test_sum_two_real_numbers },
        { "model: sum two real numbers with exception", test_sum_two_real_numbers_with_exception },

        { "scanner: one identifier", test_scan_identifier },
        { "scanner: new line (rows and columns counting)", test_scan_new_line },
        { "scanner: 'hello world' program", test_scan_hello_world },
        { "scanner: not-ASCII (cyrillic) identifier", test_scan_cyrillic_identifier },
        { "scanner: unknown symbol exception", test_unknown_symbol_exception },
        { "scanner: brackets processor with 'hello world' program", test_scan_and_process_hello_world },
        { "scanner: opening bracket without closing", test_opening_bracket_without_closing },
        { "scanner: closing bracket without opening", test_closing_bracket_without_opening },
        { "scanner: brackets do not match", test_brackets_do_not_match },
        { "scanner: integer numbers", test_scan_integer },

        { "parser: 'hello world' program", test_parsing_hello_world },
        { "parser: integer number", test_parsing_integer }
    };

    unsigned int get_number_of_tests() {
        return sizeof(test_list) / sizeof(test_description);
    }

    const test_description * get_tests() {
        return test_list;
    }
}
