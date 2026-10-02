/**
 * @file test_macro.h
 * @copyright 2026 Ivan Kniazkov
 * @brief A set of macros to facilitate writing unit tests.
 */

#pragma once

/** @brief A simple assertion macro to check boolean expressions. */
#define ASSERT(expr)                                                                               \
    if (!(expr)) {                                                                                 \
        printf("Assertion failed on line %d\n", __LINE__);                                         \
        return false;                                                                              \
    }
