/**
 * @file test_macro.h
 * @copyright 2025 Ivan Kniazkov
 * @brief A set of macros to facilitate writing unit tests.
 */

#pragma once

/**
 * @brief A simple assertion macro to check boolean expressions.
 * 
 * This macro checks the truthiness of an expression. If the expression evaluates to `false`,
 * an error message is printed with the line number, and the function returns `false`.
 * 
 * @param expr The expression to evaluate.
 */
#define ASSERT(expr) if (!(expr)) { \
    printf("Assertion failed on line %d\n", __LINE__); \
    return false;\
}
