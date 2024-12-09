/**
 * @file test_list.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Source file for managing and retrieving unit tests
 */

#include "test_list.h"
#include "test_lib.h"
#include "test_model.h"

static bool stub() {
    return true;
}

static test_description_t test_list[] = {
    { "memory allocation", test_memory_allocation }
    , { "AVL tree", test_avl_tree }

    , {"integer object", test_integer_object}
};

int get_number_of_tests() {
    return sizeof(test_list) / sizeof(test_description_t);
}

const test_description_t *get_tests() {
    return test_list;
}
