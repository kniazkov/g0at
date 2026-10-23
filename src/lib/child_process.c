/** @file child_process.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Bounded capture and absolute deadlines.
 */
#include "child_process_internal.h"

#include <string.h>

child_deadline_t child_deadline_after(uint64_t timeout_ms) {
    if (!timeout_ms)
        return 0;
    uint64_t now = child_monotonic_ms();
    return timeout_ms > UINT64_MAX - now ? UINT64_MAX : now + timeout_ms;
}

void child_capture(child_output_t *output, const unsigned char *bytes, size_t size) {
    size_t available = CHILD_OUTPUT_LIMIT - output->size;
    size_t keep = size < available ? size : available;
    memcpy(output->data + output->size, bytes, keep);
    output->size += keep;
    output->truncated |= keep < size;
}
