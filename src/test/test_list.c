/**
 * @file test_list.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Source file for managing and retrieving unit tests
 */

#include "test_list.h"

static bool stub() {
    return true;
}

static test_description_t test_list[] = {
    { "nothing", stub }
};

int get_number_of_tests() {
    return sizeof(test_list) / sizeof(test_description_t);
}

const test_description_t *get_tests() {
    return test_list;
}
