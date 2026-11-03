/** @file monotonic.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Test-only high-resolution monotonic wall clock, in nanoseconds.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
bool test_monotonic_ns(uint64_t *value);
