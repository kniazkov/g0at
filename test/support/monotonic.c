/** @file monotonic.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Linux monotonic clock and Windows performance-counter adapter.
 */
#ifndef _WIN32
#    define _POSIX_C_SOURCE 200809L
#endif
#include "test/support/monotonic.h"
#ifdef _WIN32
#    include <windows.h>
#else
#    include <time.h>
#endif
bool test_monotonic_ns(uint64_t *value) {
    if (!value)
        return false;
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0
        || !QueryPerformanceCounter(&counter) || counter.QuadPart < 0)
        return false;
    uint64_t ticks = (uint64_t)counter.QuadPart, rate = (uint64_t)frequency.QuadPart;
    uint64_t seconds = ticks / rate, remainder = ticks % rate;
    if (seconds > UINT64_MAX / 1000000000ULL || remainder > UINT64_MAX / 1000000000ULL)
        return false;
    uint64_t fraction = remainder * 1000000000ULL / rate;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) || now.tv_sec < 0 || now.tv_nsec < 0
        || now.tv_nsec >= 1000000000L)
        return false;
    uint64_t seconds = (uint64_t)now.tv_sec, fraction = (uint64_t)now.tv_nsec;
    if (seconds > UINT64_MAX / 1000000000ULL)
        return false;
#endif
    uint64_t whole = seconds * 1000000000ULL;
    if (fraction > UINT64_MAX - whole)
        return false;
    *value = whole + fraction;
    return true;
}
