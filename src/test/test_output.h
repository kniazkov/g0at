/** @file test_output.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared console format for test runners.
 */
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

/** @brief Announces a test group before its diagnostics. */
static inline void test_output_start(const char *group) {
    printf("Starting %s testing...\n", group);
    fflush(stdout);
}

/** @brief Prints a fixed-width status followed by the case name. */
static inline void test_output_case(bool passed, const char *format, ...) {
    printf("%s ", passed ? "[ ok ]" : "[fail]");
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    putchar('\n');
    fflush(stdout);
}

/** @brief Prints the same totals for every group. */
static inline void test_output_summary(const char *group, size_t passed, size_t failed) {
    printf("%s testing done; total: %zu, passed: %zu, failed: %zu\n",
           group,
           passed + failed,
           passed,
           failed);
    fflush(stdout);
}
