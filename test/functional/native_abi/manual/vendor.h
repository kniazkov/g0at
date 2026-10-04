/** @file vendor.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Private device API, independent of Goat.
 */
#pragma once
#include <stdint.h>
int vendor_read(int64_t channel, double *value);
unsigned vendor_call_count(void);
