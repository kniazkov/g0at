/**
 * @file test_list.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Source file for managing and retrieving unit tests.
 */

#include "test_list.h"
#include "test_lib.h"
#include "test_scanner.h"
#include "test_model.h"
#include "test_codegen.h"

static bool stub() {
    return true;
}

static test_description_t test_list[] = {
      { "parsing identifier", test_identifier }
    , { "parsing bracket", test_bracket }
    , { "unknown symbol", test_uknown_symbol }
      
    , { "memory allocation", test_memory_allocation }
    , { "AVL tree", test_avl_tree }
    , { "string builder", test_string_builder }
    , { "binary search", test_binary_search }
    , { "double to string", test_double_to_string }
    , { "format string", test_format_string }

    , { "boolean object", test_boolean_object }
    , { "integer object", test_integer_object }
    , { "addition of two integers", test_addition_of_two_integers }
    , { "subtraction of two integers", test_subtraction_of_two_integers }
    , { "string concatenation", test_strings_concatenation }
    , { "properties", test_properties }
    , { "string topology", test_string_topology }
    , { "store and load", test_store_and_load }
    , { "'sign' function", test_sign_function }

    , { "data builder", test_data_builder }
    , { "linker", test_linker }
};

int get_number_of_tests() {
    return sizeof(test_list) / sizeof(test_description_t);
}

const test_description_t *get_tests() {
    return test_list;
}
