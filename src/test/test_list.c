/*
    Copyright 2025 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
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
