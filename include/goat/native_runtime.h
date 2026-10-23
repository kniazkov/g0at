/** @file native_runtime.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Header-only support library for generated Goat numeric modules.
 * Distributed under the MIT license; see LICENSE.txt.
 */
#pragma once
#ifndef GOAT_NATIVE_RUNTIME_V1_H
#    define GOAT_NATIVE_RUNTIME_V1_H
#    include "native_abi.h"
#    include "native_strings.h"
#    include "unicode.h"

#    include <float.h>
#    include <math.h>
#    include <stdbool.h>
_Static_assert(sizeof(double) == 8 && FLT_RADIX == 2 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "binary64 required");
#    ifdef __FAST_MATH__
#        error Goat C arithmetic requires strict floating-point semantics
#    endif

#    ifndef GOAT_C_NUMERIC_HELPERS
#        define GOAT_C_NUMERIC_HELPERS
/** @brief Selects overflow intrinsics when supported; tests may force the portable path. */
#        if !defined(GOAT_FORCE_PORTABLE_INTEGER_MATH)
#            if defined(__has_builtin)
#                if __has_builtin(__builtin_add_overflow) && __has_builtin(__builtin_sub_overflow) \
                    && __has_builtin(__builtin_mul_overflow)
#                    define GOAT_INTEGER_OVERFLOW_BUILTINS 1
#                endif
#            elif defined(__GNUC__) && __GNUC__ >= 5
#                define GOAT_INTEGER_OVERFLOW_BUILTINS 1
#            endif
#        endif

#    endif

#    ifndef GOAT_C_I64_ADD
#        define GOAT_C_I64_ADD

static inline int64_t g_i64_add(int64_t a, int64_t b) {
#        ifdef GOAT_INTEGER_OVERFLOW_BUILTINS
    int64_t result;
    if (__builtin_add_overflow(a, b, &result))
        return a >= 0 ? INT64_MAX : INT64_MIN;
    return result;
#        else
    if (b > 0 && a > INT64_MAX - b)
        return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b)
        return INT64_MIN;
    return a + b;
#        endif
}
#    endif

#    ifndef GOAT_C_I64_SUB
#        define GOAT_C_I64_SUB

static inline int64_t g_i64_sub(int64_t a, int64_t b) {
#        ifdef GOAT_INTEGER_OVERFLOW_BUILTINS
    int64_t result;
    if (__builtin_sub_overflow(a, b, &result))
        return a >= 0 ? INT64_MAX : INT64_MIN;
    return result;
#        else
    if (b < 0 && a > INT64_MAX + b)
        return INT64_MAX;
    if (b > 0 && a < INT64_MIN + b)
        return INT64_MIN;
    return a - b;
#        endif
}
#    endif

#    ifndef GOAT_C_I64_MUL
#        define GOAT_C_I64_MUL

static inline int64_t g_i64_mul(int64_t a, int64_t b) {
#        ifdef GOAT_INTEGER_OVERFLOW_BUILTINS
    int64_t result;
    if (__builtin_mul_overflow(a, b, &result))
        return (a < 0) == (b < 0) ? INT64_MAX : INT64_MIN;
    return result;
#        else
    uint64_t x = a < 0 ? UINT64_C(0) - (uint64_t)a : (uint64_t)a;
    uint64_t y = b < 0 ? UINT64_C(0) - (uint64_t)b : (uint64_t)b;
    int negative = (a < 0) != (b < 0);
    uint64_t limit = negative ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX;
    if (y != 0 && x > limit / y)
        return negative ? INT64_MIN : INT64_MAX;
    uint64_t product = x * y;
    if (negative && product == (uint64_t)INT64_MAX + 1)
        return INT64_MIN;
    return negative ? -(int64_t)product : (int64_t)product;
#        endif
}
#    endif

#    ifndef GOAT_C_I64_NEG
#        define GOAT_C_I64_NEG

static inline int64_t g_i64_neg(int64_t value) {
    return value == INT64_MIN ? INT64_MAX : -value;
}
#    endif

#    ifndef GOAT_C_COMPARISON_HELPERS
#        define GOAT_C_COMPARISON_HELPERS

static inline unsigned g_compare_i64_double(int64_t integer, double real) {
    if (isnan(real))
        return 8;
    if (real >= 0x1p63)
        return 1;
    if (real < -0x1p63)
        return 4;
    int64_t truncated = (int64_t)real;
    if (integer < truncated)
        return 1;
    if (integer > truncated)
        return 4;
    double whole;
    double fraction = modf(real, &whole);
    return fraction > 0 ? 1 : fraction < 0 ? 4 : 2;
}
#    endif

#    include <setjmp.h>
#    if !defined(__GNUC__) && !defined(__clang__)
#        error Bounded Goat functions require GCC or Clang noinline support
#    endif
typedef struct {
    jmp_buf recovery;
    unsigned depth;
    uintptr_t origin;
} g_native_guard_t;

static _Thread_local g_native_guard_t *g_guard;

static inline void g_guard_enter(void) {
    if (!g_guard)
        return;
    char position;
    uintptr_t here = (uintptr_t)&position;
    uintptr_t distance = here > g_guard->origin ? here - g_guard->origin : g_guard->origin - here;
    if (g_guard->depth >= 32 || distance >= 65536)
        longjmp(g_guard->recovery, 1);
    g_guard->depth++;
}

static inline void g_guard_leave(void) {
    if (g_guard)
        g_guard->depth--;
}

#endif
