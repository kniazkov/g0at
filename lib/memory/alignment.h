/** @file alignment.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Fundamental allocation alignment independent of CRT header order.
 */
#pragma once

#include <stddef.h>

/** @brief Includes GCC's i386 scalar alignment omitted by MinGW's max_align_t. */
typedef union {
    max_align_t standard;
#if defined(__i386__) && defined(__SIZEOF_FLOAT128__)
    __float128 extended;
#endif
} memory_alignment_t;
