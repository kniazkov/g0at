/** @file number_format.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared VM and native real-number text formatting.
 */
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include "lib/numbers/number_format.h"

#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Uses a C numeric locale without modifying the process locale. */
static int numeric_snprintf(char *buffer, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
#ifdef _WIN32
    _locale_t locale = _create_locale(LC_NUMERIC, "C");
    if (!locale)
        abort();
    int result = _vsnprintf_l(buffer, size, format, locale, args);
    _free_locale(locale);
#else
    locale_t locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!locale)
        abort();
    locale_t previous = uselocale(locale);
    int result = vsnprintf(buffer, size, format, args);
    uselocale(previous);
    freelocale(locale);
#endif
    va_end(args);
    return result;
}

double parse_ascii_real(const char *text, char **end) {
#ifdef _WIN32
    _locale_t locale = _create_locale(LC_NUMERIC, "C");
    if (!locale)
        abort();
    double result = _strtod_l(text, end, locale);
    _free_locale(locale);
#else
    locale_t locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!locale)
        abort();
    double result = strtod_l(text, end, locale);
    freelocale(locale);
#endif
    return result;
}

void double_to_string(double value, char *buffer, size_t buffer_size) {
    char text[32];
    if (isnan(value)) {
        strcpy(text, signbit(value) ? "-nan" : "nan");
    } else if (isinf(value)) {
        strcpy(text, signbit(value) ? "-inf" : "inf");
    } else if ((value >= 1e-10 && value <= 1e10) || (value <= -1e-10 && value >= -1e10)) {
        numeric_snprintf(text, sizeof(text), "%.11f", value);
        char *dot = strchr(text, '.');
        if (dot) {
            char *end = dot + strlen(dot) - 1;
            while (end > dot && *end == '0')
                *end-- = 0;
            if (*end == '.')
                *(end + 1) = '0';
        }
    } else {
        numeric_snprintf(text, sizeof(text), "%g", value);
        /* Legacy Windows CRTs pad exponents to three digits; C99 uses at least two. */
        char *exponent = strchr(text, 'e');
        if (exponent) {
            char *digits = exponent + 1;
            if (*digits == '+' || *digits == '-')
                ++digits;
            while (digits[0] == '0' && strlen(digits) > 2)
                memmove(digits, digits + 1, strlen(digits));
        }
    }
    if (buffer_size) {
        size_t length = strlen(text);
        if (length >= buffer_size)
            length = buffer_size - 1;
        memcpy(buffer, text, length);
        buffer[length] = 0;
    }
}
