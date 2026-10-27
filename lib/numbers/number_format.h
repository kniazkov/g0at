/** @file number_format.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared VM and native real-number text formatting.
 */
#pragma once
#include <stddef.h>

/**
 * @brief Formats a double for display.
 * Uses 11 fractional digits for magnitudes in [1e-10, 1e10], trimming trailing zeros
 * but keeping one fractional digit; otherwise uses %g. Output is bounded by buffer_size.
 */
void double_to_string(double value, char *buffer, size_t buffer_size);

/** @brief Parses an ASCII real using the C numeric locale, independently of process settings. */
double parse_ascii_real(const char *text, char **end);
