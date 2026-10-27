/** @file vendor.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Simulates an externally compiled device library.
 */
#include "vendor.h"

static unsigned calls;

int vendor_read(int64_t channel, double *value) {
    calls++;
    if (channel < 0)
        return -1;
    *value = (double)channel + 0.25;
    return 0;
}

unsigned vendor_call_count(void) {
    return calls;
}
