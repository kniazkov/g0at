/** @file test_alignment_header_order.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Counterpart to the CRT-first allocation tests in test_lib_safety.c.
 */
#include "lib/alignment.h"

#include <stdio.h>

unsigned int alignment_from_header_first(void) {
    return _Alignof(memory_alignment_t);
}
