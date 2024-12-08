/**
 * @file test_list.h
 * @copyright 2025 Ivan Kniazkov
 * @brief A set of macros to facilitate writing unit tests
 */

#pragma once

#define ASSERT(expr) if (!(expr)) { \
    printf("Assertion failed on line %d\n", __LINE__); \
    return false;\
}
